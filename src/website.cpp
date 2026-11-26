#include "website.h"
#include "commonFS.h"
#include "api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include <Update.h>
#include "display.h"
#include "ota.h"
#include "config.h"
#include "debug.h"
#include "lang.h"
#include "wlan.h"
#include "generated/web_assets.h"

#ifndef VERSION
  #define VERSION "1.2.0"
#endif

#define NO_CACHE "no-cache, no-store, must-revalidate"
#define CACHE_ASSETS "max-age=86400"  // 24h Cache für statische Assets

AsyncWebServer server(webserverPort);
AsyncWebSocket ws("/ws");

uint8_t lastSuccess = 0;
nfcReaderStateType lastnfcReaderState = NFC_IDLE;

// Template-Processor für dynamische Seiten (ESPAsyncWebServer built-in)
// Ersetzt %variable% Platzhalter in HTML-Dateien
String templateProcessor(const String& var) {
    if (var == "registered") return filamanRegistered ? "Registered" : "Not Registered";
    if (var == "filamanUrl") return filamanUrl;
    if (var == "autoTare") return autoTare ? "checked" : "";
    return String();  // Unbekannte Variable - leer zurückgeben
}

// Liefert eine eingebettete Webdatei aus dem Flash aus (gzip bzw. Template-Processor).
static void sendWebAsset(AsyncWebServerRequest *request, const char* path, const char* cacheControl) {
    for (size_t i = 0; i < WEB_ASSET_COUNT; i++) {
        const WebAsset& a = WEB_ASSETS[i];
        if (strcmp(a.path, path) != 0) continue;
        AsyncWebServerResponse *response = request->beginResponse(
            200, a.mime, a.data, a.len, a.tmpl ? AwsTemplateProcessor(templateProcessor) : AwsTemplateProcessor(nullptr));
        if (a.gzip) response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", cacheControl);
        request->send(response);
        return;
    }
    request->send(404, "text/plain", "Not found");
}

void sendNfcDataToClient(AsyncWebSocketClient *client) {
    if(!client) return;
    switch(nfcReaderState){
        case NFC_IDLE: client->text("{\"type\":\"nfcData\", \"payload\":{}}"); break;
        case NFC_READ_SUCCESS: client->text("{\"type\":\"nfcData\", \"payload\":" + nfcJsonData + "}"); break;
        case NFC_READ_ERROR: client->text("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Read Error\"}}"); break;
        case NFC_WRITING: client->text("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Writing...\"}}"); break;
        case NFC_WRITE_SUCCESS: client->text("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Success\"}}"); break;
        case NFC_WRITE_ERROR: client->text("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Write Error\"}}"); break;
        default: break;
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WS Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        sendNfcDataToClient(client);
        client->text("{\"type\":\"nfcTag\", \"payload\":{\"found\": " + String(lastSuccess) + "}}");
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WS Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("WS Client #%u error: %u\n", client->id(), *((uint16_t*)arg));
    } else if (type == WS_EVT_DATA) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (char*)data, len);
        if (error) return;

        if (doc["type"] == "heartbeat") {
            ws.text(client->id(), "{"
                "\"type\":\"heartbeat\","
                "\"freeHeap\":" + String(ESP.getFreeHeap()/1024) + ","
                "\"filaman_connected\":" + String(filamanConnected) + ","
                "\"registered\":" + String(filamanRegistered) + ","
                "\"autoTare\":" + String(autoTare ? "true" : "false") + ""
                "}");
        }
        else if (doc["type"] == "writeNfcTag") {
            if (doc["payload"].is<JsonObject>()) {
                String payloadString;
                serializeJson(doc["payload"], payloadString);
                startWriteJsonToTag((doc["tagType"] == "spool") ? true : false, payloadString.c_str());
            }
        }
        else if (doc["type"] == "scale") {
            if (doc["payload"] == "tare") {
                scaleTareRequest = true;
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            }
            else if (doc["payload"] == "calibrate") {
                scaleCalibrationRequest = true;
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            }
            else if (doc["payload"] == "setAutoTare") {
                setAutoTare(doc["enabled"].as<bool>());
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            }
        }
        else if (doc["type"] == "reconnect") {
            if (doc["payload"] == "filaman") {
                sendHeartbeatAsync();
            }
        }
    }
}

void sendWriteResult(AsyncWebSocketClient *client, uint8_t success) {
    String response = "{\"type\":\"writeNfcTag\",\"success\":" + String(success ? "1" : "0") + "}";
    if (client) client->text(response); else ws.textAll(response);
}

// Ergebnis der asynchronen Geräte-Registrierung. Der HTTP-Request ist zu diesem
// Zeitpunkt längst beantwortet (202), das Frontend wartet auf diese WS-Nachricht.
void sendRegisterResult(bool success) {
    ws.textAll("{\"type\":\"registerResult\",\"success\":" + String(success ? "true" : "false") + "}");
}

void foundNfcTag(AsyncWebSocketClient *client, uint8_t success) {
    if (success == lastSuccess && client == nullptr) return;
    ws.textAll("{\"type\":\"nfcTag\", \"payload\":{\"found\": " + String(success) + "}}");
    lastSuccess = success;
}

void sendNfcData() {
    switch(nfcReaderState){
        case NFC_IDLE: ws.textAll("{\"type\":\"nfcData\", \"payload\":{}}"); break;
        case NFC_READ_SUCCESS: ws.textAll("{\"type\":\"nfcData\", \"payload\":" + nfcJsonData + "}"); break;
        case NFC_READ_ERROR: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Read Error\"}}"); break;
        case NFC_WRITING: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Writing...\"}}"); break;
        case NFC_WRITE_SUCCESS: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Success\"}}"); break;
        case NFC_WRITE_ERROR: ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Write Error\"}}"); break;
        default: break;
    }
}

void setupWebserver(AsyncWebServer &server) {
    oledShowProgressBar(2, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_WEBSERVER_INIT));

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // ===== WEBOBERFLAECHE AUS DEM FLASH =====
    // Alle Dateien aus data/ sind per scripts/embed_web.py in die Firmware
    // eingebettet (src/generated/web_assets.h). Statische Dateien sind gzip-
    // komprimiert, Template-Seiten (waage, setup) unkomprimiert.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/index.html", NO_CACHE);
    });
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/wifi.html", NO_CACHE);
    });
    server.on("/upgrade", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/upgrade.html", NO_CACHE);
    });
    server.on("/waage", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/waage.html", NO_CACHE);
    });
    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/setup.html", NO_CACHE);
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/style.css", CACHE_ASSETS);
    });
    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/logo.png", CACHE_ASSETS);
    });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        sendWebAsset(request, "/favicon.ico", CACHE_ASSETS);
    });
    server.on("/version.txt", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", VERSION);
    });

    // API Routes
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["url"] = filamanUrl;
        doc["registered"] = filamanRegistered;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/register", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        // WICHTIG: registerDevice() macht einen blockierenden HTTPClient-Call
        // (5s Timeout). Direkt hier aufgerufen friert das den AsyncTCP-Task ein
        // und reisst alle laufenden HTTP-/WebSocket-Verbindungen ab. Daher nur
        // einreihen und sofort antworten; das Ergebnis kommt per WebSocket
        // ("registerResult") vom filamanApiTask.
        String url = doc["url"].is<String>() ? doc["url"].as<String>() : String("");
        if (registerDeviceAsync(url, doc["code"].as<String>())) {
            request->send(202, "application/json", "{\"success\": true, \"pending\": true}");
        } else {
            request->send(503, "application/json", "{\"success\": false, \"error\": \"API queue full\"}");
        }
    });

    server.on("/api/v1/rfid/write", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
            return;
        }

        // Check if NFC is busy
        if (nfcWriteInProgress) {
            request->send(503, "application/json", "{\"error\": \"NFC busy\"}");
            return;
        }

        String payloadString;
        serializeJson(doc, payloadString);

        bool hasSpoolId = !doc["spool_id"].isNull() || !doc["sm_id"].isNull();
        int spoolId = doc["spool_id"] | 0;
        if (spoolId == 0 && doc["sm_id"].is<String>()) {
            spoolId = doc["sm_id"].as<String>().toInt();
        }
        int locationId = doc["location_id"] | 0;

        startWriteJsonToTag(hasSpoolId, payloadString.c_str(), spoolId, locationId);

        // Respond immediately
        request->send(200, "application/json", "{\"success\": true, \"message\": \"Schreibvorgang wurde gestartet. Bitte Tag bereit halten...\"}");
    });

    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"version\": \"" VERSION "\"}");
    });

    // Language API
    server.on("/api/language", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["lang"] = getLangCode();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/language", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        String lang = doc["lang"] | "";
        if (lang == "de") {
            saveLanguage(LANG_DE);
        } else if (lang == "en") {
            saveLanguage(LANG_EN);
        } else {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid language\"}");
            return;
        }
        request->send(200, "application/json", "{\"success\": true}");
    });

    // Display Settings API
    server.on("/api/display", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["sleepTimeout"] = oledSleepTimeout;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/display", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["sleepTimeout"].is<int>()) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing sleepTimeout\"}");
            return;
        }
        uint16_t timeout = (uint16_t)constrain((int)doc["sleepTimeout"], 0, 3600);
        saveOledSleepTimeout(timeout);
        request->send(200, "application/json", "{\"success\": true}");
    });

    // NFC-Einstellung: NFC nur bei stabilem Gewicht (Standard aus)
    server.on("/api/nfc-gate", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["enabled"] = (bool)nfcStableOnly;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/nfc-gate", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["enabled"].is<bool>()) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing enabled\"}");
            return;
        }
        saveNfcStableOnly(doc["enabled"].as<bool>());
        request->send(200, "application/json", "{\"success\": true}");
    });

    // Einstellung: Geste "Spule voll" per Langdruck (Standard aus)
    server.on("/api/spool-full-gesture", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["enabled"] = (bool)spoolFullGestureEnabled;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/spool-full-gesture", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["enabled"].is<bool>()) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing enabled\"}");
            return;
        }
        saveSpoolFullGesture(doc["enabled"].as<bool>());
        request->send(200, "application/json", "{\"success\": true}");
    });

    // WiFi TX Power API (Wert in 0.1 dBm, z.B. 195 = 19.5dBm)
    server.on("/api/wifi-power", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["txPower"] = wifiTxPowerDeciDbm;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/wifi-power", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const uint8_t*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["txPower"].is<int>()) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing txPower\"}");
            return;
        }
        int16_t txPower = (int16_t)doc["txPower"].as<int>();
        if (!isValidTxPowerDeciDbm(txPower)) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid txPower\"}");
            return;
        }
        saveWifiTxPower(txPower);
        request->send(200, "application/json", "{\"success\": true}");
    });

    server.on("/api/v1/rfid/scan-request", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        setScanRequest(true);
        request->send(200, "application/json", "{\"success\": true}");
    });

    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });

    handleUpdate(server);

    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("Webserver gestartet");
}
