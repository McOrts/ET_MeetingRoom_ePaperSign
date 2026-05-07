#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASSWORD";

// ===============================
// reTerminal E1002 authentication
// ===============================

const char* EXPECTED_NUM_SALA = "SALA_01";
const char* EXPECTED_CLAVE = "DEVICE_SECRET_123";

// ===============================
// SharePoint authentication
// ===============================

// Replace this with your real OAuth/Bearer token.
// In production, avoid hardcoding long-lived tokens.
// Prefer retrieving or refreshing the token securely.
String accessToken = "YOUR_SHAREPOINT_ACCESS_TOKEN";

const char* SHAREPOINT_URL =
  "https://sharepoint.company.local/sites/reservas/_api/web/lists/getbytitle('Reservas')/items"
  "?$select=Title,Division,Seccion,InicioReservaSala,FinalizacionReservaSala"
  "&$orderby=InicioReservaSala%20asc";

WebServer server(80);

const char* HEADERS_JSON =
  "[\"Asunto\",\"División\",\"Sección\",\"Inicio\",\"Fin\"]";

String cachedRowsJson = "[]";
unsigned long lastSharePointFetchMs = 0;
const unsigned long CACHE_TTL_MS = 60000;

// ===============================
// Helpers
// ===============================

bool isAuthorizedDevice() {
  if (!server.hasHeader("numSala") || !server.hasHeader("clave")) {
    return false;
  }

  String numSala = server.header("numSala");
  String clave = server.header("clave");

  return numSala == EXPECTED_NUM_SALA && clave == EXPECTED_CLAVE;
}

void sendUnauthorized() {
  server.send(
    401,
    "application/json",
    "{\"error\":\"Unauthorized. Missing or invalid numSala/clave headers\"}"
  );
}

String normalizeDateTime(String value) {
  // SharePoint may return "2026-04-29T09:00:00Z"
  // Device expects "2026-04-29T09:00"
  if (value.length() >= 16) {
    return value.substring(0, 16);
  }

  return value;
}

// ===============================
// SharePoint fetch + transform
// ===============================

bool refreshRowsFromSharePoint() {
  if (accessToken.length() == 0 || accessToken == "YOUR_SHAREPOINT_ACCESS_TOKEN") {
    Serial.println("Missing SharePoint access token");
    return false;
  }

  HTTPClient http;

  http.begin(SHAREPOINT_URL);

  http.addHeader("Accept", "application/json;odata=nometadata");

  String bearerHeader = "Bearer " + accessToken;
  http.addHeader("Authorization", bearerHeader);

  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.printf("SharePoint request failed. HTTP code: %d\n", code);

    String errorBody = http.getString();
    Serial.println(errorBody);

    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument spDoc(32768);
  DeserializationError error = deserializeJson(spDoc, payload);

  if (error) {
    Serial.printf("SharePoint JSON parse error: %s\n", error.c_str());
    return false;
  }

  JsonArray items = spDoc["value"].as<JsonArray>();

  DynamicJsonDocument outDoc(16384);
  JsonArray rows = outDoc.to<JsonArray>();

  for (JsonObject item : items) {
    JsonArray row = rows.createNestedArray();

    const char* asunto = item["Title"] | "";
    const char* division = item["Division"] | "";
    const char* seccion = item["Seccion"] | "";
    const char* inicio = item["InicioReservaSala"] | "";
    const char* fin = item["FinalizacionReservaSala"] | "";

    row.add(asunto);
    row.add(division);
    row.add(seccion);
    row.add(normalizeDateTime(String(inicio)));
    row.add(normalizeDateTime(String(fin)));
  }

  cachedRowsJson = "";
  serializeJson(rows, cachedRowsJson);

  lastSharePointFetchMs = millis();

  Serial.println("SharePoint data refreshed successfully");
  return true;
}

// ===============================
// HTTP handlers
// ===============================

void handleRooms() {
  if (!isAuthorizedDevice()) {
    sendUnauthorized();
    return;
  }

  if (!server.hasArg("part")) {
    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing parameter: part\"}"
    );
    return;
  }

  String part = server.arg("part");

  if (part == "headers") {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json; charset=utf-8", HEADERS_JSON);
    return;
  }

  if (part == "data") {
    bool cacheExpired =
      cachedRowsJson == "[]" ||
      millis() - lastSharePointFetchMs > CACHE_TTL_MS;

    if (cacheExpired) {
      bool ok = refreshRowsFromSharePoint();

      if (!ok && cachedRowsJson == "[]") {
        server.send(
          502,
          "application/json",
          "{\"error\":\"SharePoint unavailable or unauthorized\"}"
        );
        return;
      }
    }

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json; charset=utf-8", cachedRowsJson);
    return;
  }

  server.send(
    400,
    "application/json",
    "{\"error\":\"Invalid part. Use headers or data\"}"
  );
}

void handleHealth() {
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ===============================
// Setup / loop
// ===============================

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wrapper IP: ");
  Serial.println(WiFi.localIP());

  // Required so WebServer captures custom headers.
  const char* headerKeys[] = {
    "numSala",
    "clave"
  };

  size_t headerKeyCount = sizeof(headerKeys) / sizeof(char*);
  server.collectHeaders(headerKeys, headerKeyCount);

  server.on("/rooms", HTTP_GET, handleRooms);
  server.on("/health", HTTP_GET, handleHealth);

  server.begin();

  Serial.println("XIAO ESP32-S3 API Wrapper started");
}

void loop() {
  server.handleClient();
}