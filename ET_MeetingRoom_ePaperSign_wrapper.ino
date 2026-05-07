/*
 * ET_MeetingRoom_ePaperSign_wrapper
 *
 * Lightweight REST API wrapper running on a Seeed Studio XIAO ESP32-S3.
 * Bridges a reTerminal E1002 ePaper Display with a SharePoint Server 2019
 * room-booking list.
 *
 * Endpoints
 *   GET /health              – liveness check (no auth required)
 *   GET /rooms?part=headers  – returns column-name array
 *   GET /rooms?part=data     – returns 2-D booking array
 *
 * Required request headers for /rooms:
 *   numSala  – room identifier
 *   clave    – shared secret
 *
 * See include/config.example.h for configuration placeholders.
 *
 * MIT License – Copyright (c) 2026 Carlos Orts
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "include/config.h"   // copy from include/config.example.h and fill in values

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void handleHealth();
void handleRooms();
bool validateHeaders(WebServer &server);
bool fetchSharePoint();
String normaliseDateTime(const String &raw);

// ---------------------------------------------------------------------------
// Cache state
// ---------------------------------------------------------------------------
static String  cachedJson      = "";        // serialised 2-D JSON array
static unsigned long cacheTime = 0;         // millis() at last successful fetch

// ---------------------------------------------------------------------------
// Web server (port 80)
// ---------------------------------------------------------------------------
WebServer server(80);

// ===========================================================================
// setup()
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("[INFO] ET_MeetingRoom_ePaperSign_wrapper starting…");

  // --- Wi-Fi ----------------------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[INFO] Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("[INFO] Connected. IP address: ");
  Serial.println(WiFi.localIP());

  // --- Routes ---------------------------------------------------------------
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/rooms",  HTTP_GET, handleRooms);

  // 404 handler
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
  });

  server.begin();
  Serial.println("[INFO] HTTP server started.");
}

// ===========================================================================
// loop()
// ===========================================================================
void loop() {
  server.handleClient();
}

// ===========================================================================
// Handlers
// ===========================================================================

// GET /health
void handleHealth() {
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// GET /rooms?part=headers|data
void handleRooms() {
  // -- Validate custom headers -----------------------------------------------
  if (!validateHeaders(server)) {
    server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  // -- Validate query parameter ----------------------------------------------
  if (!server.hasArg("part")) {
    server.send(400, "application/json",
                "{\"error\":\"Missing query parameter: part\"}");
    return;
  }

  String part = server.arg("part");

  if (part != "headers" && part != "data") {
    server.send(400, "application/json",
                "{\"error\":\"Invalid value for parameter part. Use headers or data.\"}");
    return;
  }

  // -- Return headers immediately (no SharePoint call needed) ----------------
  if (part == "headers") {
    const char *headersJson =
      "[\"Asunto\",\"División\",\"Sección\",\"Inicio\",\"Fin\"]";
    server.send(200, "application/json", headersJson);
    return;
  }

  // -- part == "data" --------------------------------------------------------
  bool cacheValid = (cachedJson.length() > 0) &&
                    (millis() - cacheTime < CACHE_TTL_MS);

  if (!cacheValid) {
    bool ok = fetchSharePoint();
    if (!ok) {
      if (cachedJson.length() > 0) {
        // Stale cache is better than nothing
        server.send(200, "application/json", cachedJson);
      } else {
        server.send(502, "application/json",
                    "{\"error\":\"SharePoint unavailable and no cached data\"}");
      }
      return;
    }
  }

  server.send(200, "application/json", cachedJson);
}

// ===========================================================================
// Header validation
// ===========================================================================
bool validateHeaders(WebServer &srv) {
  if (!srv.hasHeader("numSala") || !srv.hasHeader("clave")) {
    return false;
  }
  if (srv.header("numSala") != String(EXPECTED_NUM_SALA)) {
    return false;
  }
  if (srv.header("clave") != String(EXPECTED_CLAVE)) {
    return false;
  }
  return true;
}

// ===========================================================================
// SharePoint fetch & transform
// ===========================================================================
bool fetchSharePoint() {
  HTTPClient http;

  http.begin(SHAREPOINT_URL);
  http.addHeader("Accept", "application/json;odata=nometadata");

  String bearerHeader = "Bearer " + String(SHAREPOINT_ACCESS_TOKEN);
  http.addHeader("Authorization", bearerHeader);

  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[ERROR] SharePoint returned HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // -- Parse OData response --------------------------------------------------
  // Allocate a generous document; adjust if the list grows large.
  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.printf("[ERROR] JSON parse failed: %s\n", err.c_str());
    return false;
  }

  JsonArray items = doc["value"].as<JsonArray>();
  if (items.isNull()) {
    Serial.println("[ERROR] SharePoint response has no 'value' array.");
    return false;
  }

  // -- Build output 2-D array ------------------------------------------------
  DynamicJsonDocument output(16384);
  JsonArray rows = output.to<JsonArray>();

  for (JsonObject item : items) {
    JsonArray row = rows.createNestedArray();

    // Asunto: prefer "Asunto" field, fall back to "Title"
    String asunto = item.containsKey("Asunto")
                      ? item["Asunto"].as<String>()
                      : item["Title"].as<String>();
    row.add(asunto);
    row.add(item["Division"].as<String>());
    row.add(item["Seccion"].as<String>());
    row.add(normaliseDateTime(item["InicioReservaSala"].as<String>()));
    row.add(normaliseDateTime(item["FinalizacionReservaSala"].as<String>()));
  }

  // -- Serialise and store in cache ------------------------------------------
  cachedJson = "";
  serializeJson(output, cachedJson);
  cacheTime = millis();

  Serial.printf("[INFO] SharePoint: fetched %d items, cache updated.\n",
                rows.size());
  return true;
}

// ===========================================================================
// Datetime normalisation
// Trims an ISO-8601 string to YYYY-MM-DDTHH:mm
// e.g. "2026-04-29T09:00:00Z"  →  "2026-04-29T09:00"
//      "2026-04-29T09:00:00"   →  "2026-04-29T09:00"
// ===========================================================================
String normaliseDateTime(const String &raw) {
  // Minimum expected length: "YYYY-MM-DDTHH:mm" = 16 characters
  if (raw.length() >= 16) {
    return raw.substring(0, 16);
  }
  return raw;
}
