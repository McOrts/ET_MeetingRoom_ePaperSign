/*
 * config.example.h
 *
 * Configuration template for ET_MeetingRoom_ePaperSign_wrapper.
 *
 * HOW TO USE
 * ----------
 * 1. Copy this file to include/config.h
 * 2. Replace every placeholder value with your real settings.
 * 3. Never commit include/config.h — it is listed in .gitignore.
 *
 * MIT License – Copyright (c) 2026 Carlos Orts
 */

#pragma once

// ---------------------------------------------------------------------------
// Wi-Fi credentials
// ---------------------------------------------------------------------------

/** SSID of the internal Wi-Fi network the XIAO ESP32-S3 should join. */
#define WIFI_SSID "YOUR_WIFI_SSID"

/** Password for the Wi-Fi network. */
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ---------------------------------------------------------------------------
// Device authentication
// These values are checked against the headers sent by the reTerminal E1002.
// ---------------------------------------------------------------------------

/** Room identifier expected in the numSala request header. */
#define EXPECTED_NUM_SALA "SALA_01"

/** Shared secret expected in the clave request header. Rotate periodically. */
#define EXPECTED_CLAVE "DEVICE_SECRET_123"

// ---------------------------------------------------------------------------
// SharePoint connectivity
// ---------------------------------------------------------------------------

/**
 * Bearer access token for SharePoint Server 2019.
 *
 * SECURITY WARNING
 * ----------------
 * Embedding a long-lived token in firmware is convenient for development but
 * is a security risk in production.  See the README for recommended
 * alternatives (short-lived OAuth tokens, internal proxy, OTA provisioning).
 *
 * Do NOT log this value to Serial output.
 */
#define SHAREPOINT_ACCESS_TOKEN "YOUR_SHAREPOINT_BEARER_TOKEN"

/**
 * Full SharePoint REST API URL for the Reservas list.
 *
 * Example:
 *   https://sharepoint.company.local/sites/reservas/_api/web/lists/
 *   getbytitle('Reservas')/items
 *   ?$select=Title,Division,Seccion,InicioReservaSala,FinalizacionReservaSala
 *   &$orderby=InicioReservaSala%20asc
 */
#define SHAREPOINT_URL \
  "https://sharepoint.company.local/sites/reservas/_api/web/lists/getbytitle('Reservas')/items" \
  "?$select=Title,Division,Seccion,InicioReservaSala,FinalizacionReservaSala" \
  "&$orderby=InicioReservaSala%20asc"

// ---------------------------------------------------------------------------
// Cache settings
// ---------------------------------------------------------------------------

/**
 * How long (in milliseconds) a successful SharePoint response is considered
 * fresh.  After this interval the wrapper will re-fetch from SharePoint on
 * the next device request.
 *
 * Default: 60 000 ms (60 seconds)
 */
#define CACHE_TTL_MS 60000
