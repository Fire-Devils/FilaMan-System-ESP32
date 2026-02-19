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

#ifndef VERSION
  #define VERSION "1.2.0"
#endif

// Cache-Control Header definieren
#define CACHE_CONTROL "max-age=604800" // Cache für 1 Woche

AsyncWebServer server(webserverPort);
AsyncWebSocket ws("/ws");

uint8_t lastSuccess = 0;
nfcReaderStateType lastnfcReaderState = NFC_IDLE;


void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    HEAP_DEBUG_MESSAGE("onWsEvent begin");
    if (type == WS_EVT_CONNECT) {
        Serial.println("Neuer Client verbunden!");
        sendNfcData();
        foundNfcTag(client, 0);
        sendWriteResult(client, 3);

        // Clean up dead connections
        (*server).cleanupClients();
        Serial.println("Currently connected number of clients: " + String((*server).getClients().size()));
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.println("Client getrennt.");
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("WebSocket Client #%u error(%u): %s\n", client->id(), *((uint16_t*)arg), (char*)data);
    } else if (type == WS_EVT_PONG) {
        Serial.printf("WebSocket Client #%u pong\n", client->id());
    } else if (type == WS_EVT_DATA) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (char*)data, len);

        if (error) {
            Serial.println("JSON deserialization failed: " + String(error.c_str()));
            return;
        }

        if (doc["type"] == "heartbeat") {
            // Sende Heartbeat-Antwort
            ws.text(client->id(), "{"
                "\"type\":\"heartbeat\","
                "\"freeHeap\":" + String(ESP.getFreeHeap()/1024) + ","
                "\"filaman_connected\":" + String(filamanConnected) + ","
                "\"registered\":" + String(filamanRegistered) + ""
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
            uint8_t success = 0;
            if (doc["payload"] == "tare") {
                scaleTareRequest = true;
                success = 1;
            }

            if (doc["payload"] == "calibrate") {
                success = calibrate_scale();
            }

            if (doc["payload"] == "setAutoTare") {
                success = setAutoTare(doc["enabled"].as<bool>());
            }

            if (success) {
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            } else {
                ws.textAll("{\"type\":\"scale\",\"payload\":\"error\"}");
            }
        }

        else if (doc["type"] == "reconnect") {
            if (doc["payload"] == "filaman") {
                sendHeartbeat();
            }
        }

        else {
            Serial.println("Unbekannter WebSocket-Typ: " + doc["type"].as<String>());
        }
        doc.clear();
    }
    HEAP_DEBUG_MESSAGE("onWsEvent end");
}

String loadHtmlWithHeader(const char* filename) {
    Serial.println("Lade HTML-Datei: " + String(filename));
    if (!LittleFS.exists(filename)) {
        Serial.println("Fehler: Datei nicht gefunden!");
        return "Fehler: Datei nicht gefunden!";
    }

    File file = LittleFS.open(filename, "r");
    String html = file.readString();
    file.close();

    return html;
}

void sendWriteResult(AsyncWebSocketClient *client, uint8_t success) {
    String response = "{\"type\":\"writeNfcTag\",\"success\":" + String(success ? "1" : "0") + "}";
    ws.textAll(response);
}

void foundNfcTag(AsyncWebSocketClient *client, uint8_t success) {
    if (success == lastSuccess) return;
    ws.textAll("{\"type\":\"nfcTag\", \"payload\":{\"found\": " + String(success) + "}}");
    sendNfcData();
    lastSuccess = success;
}

void sendNfcData() {
    if (lastnfcReaderState == nfcReaderState) return;
    switch(nfcReaderState){
        case NFC_IDLE:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":{}}");
            break;
        case NFC_READ_SUCCESS:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":" + nfcJsonData + "}");
            break;
        case NFC_READ_ERROR:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Empty Tag or Data not readable\"}}");
            break;
        case NFC_WRITING:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Schreibe Tag...\"}}");
            break;
        case NFC_WRITE_SUCCESS:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Tag erfolgreich geschrieben\"}}");
            break;
        case NFC_WRITE_ERROR:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Error writing to Tag\"}}");
            break;
        case DEFAULT:
            ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Something went wrong\"}}");
    }
    lastnfcReaderState = nfcReaderState;
}

void setupWebserver(AsyncWebServer &server) {
    oledShowProgressBar(2, 7, DISPLAY_BOOT_TEXT, "Webserver init");
    Serial.setDebugOutput(false);
    
    ws.onEvent(onWsEvent);
    ws.enable(true);

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){});
    server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){});

    loadFilamanConfig();

    // Route für about
    server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    // Route für Waage
    server.on("/waage", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = loadHtmlWithHeader("/waage.html");
        html.replace("{{autoTare}}", (autoTare) ? "checked" : "");
        request->send(200, "text/html", html);
    });

    // Route für RFID
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/rfid.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    // Route für Config & Registration
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = loadHtmlWithHeader("/config.html");
        html.replace("{{filamanUrl}}", filamanUrl);
        html.replace("{{registered}}", filamanRegistered ? "Yes" : "No");
        request->send(200, "text/html", html);
    });

    server.on("/api/register", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        JsonDocument doc;
        deserializeJson(doc, (const char*)data);
        
        if (doc["url"].is<String>()) filamanUrl = doc["url"].as<String>();
        String code = doc["code"].as<String>();
        
        saveFilamanConfig();
        
        if (registerDevice(code)) {
            request->send(200, "application/json", "{\"success\": true}");
        } else {
            request->send(400, "application/json", "{\"success\": false}");
        }
    });

    // API for Backend to trigger Tag write
    server.on("/api/v1/rfid/write", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        // Check Authorization? (TBD)
        JsonDocument doc;
        deserializeJson(doc, (const char*)data);
        
        String payloadString;
        serializeJson(doc, payloadString);
        
        bool isSpool = doc.containsKey("spool_id");
        startWriteJsonToTag(isSpool, payloadString.c_str());
        
        request->send(200, "application/json", "{\"status\": \"writing\"}");
    });

    // Route für WiFi
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/wifi.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
        ESP.restart();
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/style.css.gz", "text/css");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/logo.png.gz", "image/png");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/favicon.ico", "image/x-icon");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    server.on("/rfid.js", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS,"/rfid.js.gz", "text/javascript");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    server.on("/upgrade", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/upgrade.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });

    handleUpdate(server);

    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
        String fm_version = VERSION;
        String jsonResponse = "{\"version\": \""+ fm_version +"\"}";
        request->send(200, "application/json", jsonResponse);
    });

    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Seite nicht gefunden");
    });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    ws.enable(true);

    server.begin();
    Serial.println("Webserver gestartet");
}
