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

/* 自动生成的多语言 UI 字符串映射函数 */
String getUIString(const char* key) {
    Lang lang = currentLang;
    
    /* Navigation */
    if (strcmp(key, "NAV_HOME") == 0) {
        return (lang == LANG_DE) ? "Start" : (lang == LANG_ZH) ? "345275240345236213" : "Start";
    }
    if (strcmp(key, "NAV_SCALE") == 0) {
        return (lang == LANG_DE) ? "Waage" : (lang == LANG_ZH) ? "350201224" : "Scale";
    }
    if (strcmp(key, "NAV_SETUP") == 0) {
        return (lang == LANG_DE) ? "Setup" : (lang == LANG_ZH) ? "357274200347256200" : "Setup";
    }
    if (strcmp(key, "NAV_UPGRADE") == 0) {
        return (lang == LANG_DE) ? "Upgrade" : (lang == LANG_ZH) ? "351247210350201213" : "Upgrade";
    }
    
    /* Common UI */
    if (strcmp(key, "SYSTEM_SYNC") == 0) {
        return (lang == LANG_DE) ? "System Sync" : (lang == LANG_ZH) ? "347205247347211210346216222345274201" : "System Sync";
    }
    
    /* index.html - Welcome page */
    if (strcmp(key, "WELCOME_TITLE") == 0) {
        return (lang == LANG_DE) ? "Willkommen bei FilaMan" : (lang == LANG_ZH) ? "347275216346234254346225260347250213 FilaMan" : "Welcome to FilaMan";
    }
    if (strcmp(key, "WELCOME_SUBTITLE_1") == 0) {
        return (lang == LANG_DE) ? "Ihre smarte Lösung für" : (lang == LANG_ZH) ? "350256276347264240346241205" : "Your smart solution for";
    }
    if (strcmp(key, "WELCOME_SUBTITLE_2") == 0) {
        return (lang == LANG_DE) ? "Filament-Management" : (lang == LANG_ZH) ? "346245217346226271346241210346234254" : "Filament Management";
    }
    if (strcmp(key, "WELCOME_SUBTITLE_3") == 0) {
        return (lang == LANG_DE) ? "im 3D-Druck." : (lang == LANG_ZH) ? "3D351223262350241213345217202346234254" : "in 3D printing.";
    }
    if (strcmp(key, "CARD_COMPAT_TITLE") == 0) {
        return (lang == LANG_DE) ? "System-Kompatibilität" : (lang == LANG_ZH) ? "347205247347211210345220210347272246" : "System Compatibility";
    }
    if (strcmp(key, "COMPAT_TEXT_1") == 0) {
        return (lang == LANG_DE) ? "Ab" : (lang == LANG_ZH) ? "345212250" : "Starting with";
    }
    if (strcmp(key, "COMPAT_TEXT_2") == 0) {
        return (lang == LANG_DE) ? "Version 3.0.0" : (lang == LANG_ZH) ? "3.0.0 346241210345217202" : "Version 3.0.0";
    }
    if (strcmp(key, "COMPAT_TEXT_3") == 0) {
        return (lang == LANG_DE) ? "ist diese Waage ausschließlich für die Verwendung mit dem" : (lang == LANG_ZH) ? "368213230350201224351242230346241210347224237347250213" : "this scale is exclusively designed for use with the";
    }
    if (strcmp(key, "COMPAT_TEXT_4") == 0) {
        return (lang == LANG_DE) ? "FilaMan-System" : (lang == LANG_ZH) ? "FilaMan 347205247346234254" : "FilaMan-System";
    }
    if (strcmp(key, "FEATURE_TRACKING_TITLE") == 0) {
        return (lang == LANG_DE) ? "Filament-Verfolgung" : (lang == LANG_ZH) ? "346245217350241210346234254346224271345256214" : "Filament Tracking";
    }
    if (strcmp(key, "FEATURE_TRACKING_DESC") == 0) {
        return (lang == LANG_DE) ? "Einfaches Identifizieren von Filament-Spulen mit NFC-Tags für sofortige Rückverfolgung." : (lang == LANG_ZH) ? "347224237346210220 NFC 350201224347252231347224237345272216345257271347242272346267275345212250344275277," : "Easily identify filament spools using NFC tags for instant tracking.";
    }
    if (strcmp(key, "FEATURE_SYNC_TITLE") == 0) {
        return (lang == LANG_DE) ? "Cloud-Synchronisation" : (lang == LANG_ZH) ? "345274200344270213346213251347224237345217202" : "Cloud Sync";
    }
    if (strcmp(key, "FEATURE_SYNC_DESC") == 0) {
        return (lang == LANG_DE) ? "Nahtlose Aktualisierung der Spulendaten über die FilaMan-API für präzise Rückverfolgung." : (lang == LANG_ZH) ? "347224237 FilaMan API 346213267346272256350257257345257271350201224346241210345256214, : "Seamlessly update spool data with the FilaMan API for accurate tracking.";
    }
    if (strcmp(key, "FEATURE_WEIGH_TITLE") == 0) {
        return (lang == LANG_DE) ? "Präzises Wiegen" : (lang == LANG_ZH) ? "345266205350261215350201224" : "Precision Weighing";
    }
    if (strcmp(key, "FEATURE_WEIGH_DESC") == 0) {
        return (lang == LANG_DE) ? "Integrierte Waage-Unterstützung für exakte Restgewichtsberechnung." : (lang == LANG_ZH) ? "352273217350201224345214226347247274, : "Built-in scale support for precise remaining weight calculation.";
    }
    if (strcmp(key, "BTN_GET_STARTED") == 0) {
        return (lang == LANG_DE) ? "Loslegen" : (lang == LANG_ZH) ? "345211215350236213346234254" : "Get Started";
    }
    
    /* setup.html */
    if (strcmp(key, "SETUP_TITLE") == 0) {
        return (lang == LANG_DE) ? "Geräte-Setup" : (lang == LANG_ZH) ? "346266270350201224357274200347256200" : "Device Setup";
    }
    if (strcmp(key, "REGISTER_TITLE") == 0) {
        return (lang == LANG_DE) ? "System-Registrierung" : (lang == LANG_ZH) ? "347205247346234254346241210346224271" : "System Registration";
    }
    if (strcmp(key, "REGISTER_URL_LABEL") == 0) {
        return (lang == LANG_DE) ? "FilaMan URL" : (lang == LANG_ZH) ? "FilaMan 350257257346241210" : "FilaMan URL";
    }
    if (strcmp(key, "REGISTER_CODE_LABEL") == 0) {
        return (lang == LANG_DE) ? "Geräte-Code" : (lang == LANG_ZH) ? "346266270350201224346241210347233256" : "Device Code";
    }
    if (strcmp(key, "REGISTER_BTN") == 0) {
        return (lang == LANG_DE) ? "Gerät registrieren" : (lang == LANG_ZH) ? "346230216350201224" : "Register Device";
    }
    if (strcmp(key, "DISPLAY_TITLE") == 0) {
        return (lang == LANG_DE) ? "Anzeige-Einstellungen" : (lang == LANG_ZH) ? "346226271346241210357274200347256200" : "Display Settings";
    }
    if (strcmp(key, "DISPLAY_LANG_LABEL") == 0) {
        return (lang == LANG_DE) ? "Anzeige-Sprache" : (lang == LANG_ZH) ? "346226271346241210345244247350200203" : "Display Language";
    }
    if (strcmp(key, "DISPLAY_SLEEP_LABEL") == 0) {
        return (lang == LANG_DE) ? "OLED Sleep-Timeout" : (lang == LANG_ZH) ? "OLED 347252231345257271351242231346241210" : "OLED Sleep Timeout";
    }
    if (strcmp(key, "DISPLAY_SECONDS_LABEL") == 0) {
        return (lang == LANG_DE) ? "Sekunden" : (lang == LANG_ZH) ? "345211213" : "seconds";
    }
    if (strcmp(key, "REGISTER_ERROR_1") == 0) {
        return (lang == LANG_DE) ? "Bitte beide Felder ausfüllen (URL und Code)" : (lang == LANG_ZH) ? "352260211350257257345217202 URL 345216237346241210" : "Please provide both URL and Code";
    }
    if (strcmp(key, "REGISTER_PROGRESS") == 0) {
        return (lang == LANG_DE) ? "Registriere Gerät..." : (lang == LANG_ZH) ? "345274200347224237346266270350201224..." : "Registering device...";
    }
    if (strcmp(key, "REGISTER_SUCCESS") == 0) {
        return (lang == LANG_DE) ? "Registrierung erfolgreich!" : (lang == LANG_ZH) ? "346230216350201224351247275346234254!" : "Registration successful!";
    }
    if (strcmp(key, "REGISTER_FAILED") == 0) {
        return (lang == LANG_DE) ? "Registrierung fehlgeschlagen. Bitte Code und URL prüfen." : (lang == LANG_ZH) ? "346230216350201224351247255346234254357274225351224231346241210345217202 URL." : "Registration failed. Please check your code and URL.";
    }
    if (strcmp(key, "SETTING_SAVED") == 0) {
        return (lang == LANG_DE) ? "Gespeichert." : (lang == LANG_ZH) ? "351235242347256200." : "Saved.";
    }
    if (strcmp(key, "SETTING_ERROR") == 0) {
        return (lang == LANG_DE) ? "Fehler beim Speichern." : (lang == LANG_ZH) ? "351235242347256200351242231346234254." : "Error saving setting.";
    }
    
    /* waage.html */
    if (strcmp(key, "SCALE_TITLE") == 0) {
        return (lang == LANG_DE) ? "Waagen-Konfiguration" : (lang == LANG_ZH) ? "350201224357274200347256200" : "Scale Configuration";
    }
    if (strcmp(key, "SCALE_ACTIONS_TITLE") == 0) {
        return (lang == LANG_DE) ? "Waagen-Aktionen" : (lang == LANG_ZH) ? "350201224345265260345257271" : "Scale Actions";
    }
    if (strcmp(key, "SCALE_TARE_BTN") == 0) {
        return (lang == LANG_DE) ? "Waage tarieren" : (lang == LANG_ZH) ? "351205215350201224" : "Tare Scale";
    }
    if (strcmp(key, "SCALE_CAL_BTN") == 0) {
        return (lang == LANG_DE) ? "Waage kalibrieren" : (lang == LANG_ZH) ? "350204221347224237350201224" : "Calibrate Scale";
    }
    if (strcmp(key, "SCALE_WS_CONNECTED") == 0) {
        return (lang == LANG_DE) ? "Waage verbunden über WebSocket" : (lang == LANG_ZH) ? "350201224351223276347211210210 WebSocket" : "Scale connected via WebSocket";
    }
    if (strcmp(key, "SCALE_WS_DISCONNECTED") == 0) {
        return (lang == LANG_DE) ? "Waage-Verbindung verloren. Wiederaufbau..." : (lang == LANG_ZH) ? "350201224347232271346241210352260205350257257..." : "Scale connection lost. Reconnecting...";
    }
    if (strcmp(key, "ACTION_SUCCESS") == 0) {
        return (lang == LANG_DE) ? "Aktion erfolgreich" : (lang == LANG_ZH) ? "345257271345221212351247275" : "Action successful";
    }
    if (strcmp(key, "ACTION_ERROR") == 0) {
        return (lang == LANG_DE) ? "Fehler bei der Aktion" : (lang == LANG_ZH) ? "345257271345221212345226213346241210" : "Error while performing action";
    }
    
    /* upgrade.html */
    if (strcmp(key, "UPGRADE_TITLE") == 0) {
        return (lang == LANG_DE) ? "Firmware-Update" : (lang == LANG_ZH) ? "346250241345261217351242231" : "Firmware Upgrade";
    }
    if (strcmp(key, "UPGRADE_FILE_LABEL") == 0) {
        return (lang == LANG_DE) ? "Firmware-Datei" : (lang == LANG_ZH) ? "346250241345261217346241260351200211" : "Firmware File";
    }
    if (strcmp(key, "UPGRADE_CHOOSE_BTN") == 0) {
        return (lang == LANG_DE) ? "Datei wählen" : (lang == LANG_ZH) ? "351224231346241260" : "Choose File";
    }
    if (strcmp(key, "UPGRADE_NO_FILE") == 0) {
        return (lang == LANG_DE) ? "Keine Datei ausgewählt" : (lang == LANG_ZH) ? "347224237346210220346241260351200211" : "No file selected";
    }
    if (strcmp(key, "UPGRADE_BTN") == 0) {
        return (lang == LANG_DE) ? "Firmware aktualisieren" : (lang == LANG_ZH) ? "351242231346250241345261217" : "Update Firmware";
    }
    if (strcmp(key, "UPGRADE_FAIL_PREFIX") == 0) {
        return (lang == LANG_DE) ? "Update fehlgeschlagen: " : (lang == LANG_ZH) ? "351242231346250241345261217351247255346234254:" : "Update failed: ";
    }
    
    /* wifi.html */
    if (strcmp(key, "WIFI_TITLE") == 0) {
        return (lang == LANG_DE) ? "WiFi-Konfiguration" : (lang == LANG_ZH) ? "WiFi 357274200347256200" : "WiFi Configuration";
    }
    if (strcmp(key, "WIFI_DESC") == 0) {
        return (lang == LANG_DE) ? "Verbindungsstatus und Netzwerk-Management." : (lang == LANG_ZH) ? "347205247346234254351224271347232272345274200344270213." : "Connection status and network management.";
    }
    if (strcmp(key, "WIFI_SSID_LABEL") == 0) {
        return (lang == LANG_DE) ? "Aktuelle SSID" : (lang == LANG_ZH) ? "345244247351235243 SSID" : "Current SSID";
    }
    if (strcmp(key, "WIFI_NOT_CONNECTED") == 0) {
        return (lang == LANG_DE) ? "Nicht verbunden" : (lang == LANG_ZH) ? "347224237351223276" : "Not connected";
    }
    if (strcmp(key, "WIFI_CONNECT_BTN") == 0) {
        return (lang == LANG_DE) ? "Verbinden" : (lang == LANG_ZH) ? "351223276" : "Connect";
    }
    if (strcmp(key, "WIFI_DISCONNECT_BTN") == 0) {
        return (lang == LANG_DE) ? "Trennen" : (lang == LANG_ZH) ? "351202215345207272" : "Disconnect";
    }
    if (strcmp(key, "WIFI_SCAN_BTN") == 0) {
        return (lang == LANG_DE) ? "Netzwerke scannen" : (lang == LANG_ZH) ? "350205257347232272346216222" : "Scan Networks";
    }
    
    return "";
}
void setupWebserver(AsyncWebServer &server) {
    oledShowProgressBar(2, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_WEBSERVER_INIT));

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // ===== STATISCHE ASSETS mit serveStatic() =====
    // Optimiert für concurrent Requests, mit 24h Browser-Cache
    // serveStatic() ist nicht-blockierend und handhabt parallele Requests besser
    server.serveStatic("/style.css", LittleFS, "/style.css")
        .setCacheControl(CACHE_ASSETS);
    server.serveStatic("/logo.png", LittleFS, "/logo.png")
        .setCacheControl(CACHE_ASSETS);
    server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico")
        .setCacheControl(CACHE_ASSETS);

    // ===== STATISCHE HTML-SEITEN (ohne Template-Ersetzung) =====
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /");
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /wifi");
        request->send(LittleFS, "/wifi.html", "text/html");
    });

    server.on("/upgrade", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Web: Request /upgrade");
        request->send(LittleFS, "/upgrade.html", "text/html");
    });

    server.on("/version.txt", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/version.txt", "text/plain");
    });

    // ===== DYNAMISCHE HTML-SEITEN (mit Template-Processor) =====
    // Nutzt den eingebauten Template-Processor für %variable% Ersetzung
    // Nicht-blockierend: Datei wird gestreamt und Variablen on-the-fly ersetzt
    server.on("/waage", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /waage");
        request->send(LittleFS, "/waage.html", "text/html", false, templateProcessor);
    });

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Web: Request /setup");
        request->send(LittleFS, "/setup.html", "text/html", false, templateProcessor);
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
        if (doc["url"].is<String>()) filamanUrl = doc["url"].as<String>();
        saveFilamanConfig();
        if (registerDevice(doc["code"].as<String>())) {
            request->send(200, "application/json", "{\"success\": true}");
        } else {
            request->send(400, "application/json", "{\"success\": false}");
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
        } else if (lang == "zh") {
            saveLanguage(LANG_ZH);
        } else if (lang == "en") {
            saveLanguage(LANG_EN);
        } else {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid language\"}");
            return;
        }
        request->send(200, "application/json", "{\"success\": true}");
    });

    // Get all translatable strings for current language

    server.on("/api/lang-strings", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["lang"] = getLangCode();
        
        JsonObject strings = doc.createNestedObject("strings");
        
        // Navigation
        strings["NAV_HOME"] = getUIString("NAV_HOME");
        strings["NAV_SCALE"] = getUIString("NAV_SCALE");
        strings["NAV_SETUP"] = getUIString("NAV_SETUP");
        strings["NAV_UPGRADE"] = getUIString("NAV_UPGRADE");
        
        // Common UI
        strings["SYSTEM_SYNC"] = getUIString("SYSTEM_SYNC");
        strings["VERSION_LABEL"] = getUIString("VERSION_LABEL");
        
        // index.html - Welcome page
        strings["WELCOME_TITLE"] = getUIString("WELCOME_TITLE");
        strings["WELCOME_SUBTITLE_1"] = getUIString("WELCOME_SUBTITLE_1");
        strings["WELCOME_SUBTITLE_2"] = getUIString("WELCOME_SUBTITLE_2");
        strings["WELCOME_SUBTITLE_3"] = getUIString("WELCOME_SUBTITLE_3");
        strings["CARD_COMPAT_TITLE"] = getUIString("CARD_COMPAT_TITLE");
        strings["COMPAT_TEXT_1"] = getUIString("COMPAT_TEXT_1");
        strings["COMPAT_TEXT_2"] = getUIString("COMPAT_TEXT_2");
        strings["COMPAT_TEXT_3"] = getUIString("COMPAT_TEXT_3");
        strings["COMPAT_TEXT_4"] = getUIString("COMPAT_TEXT_4");
        strings["FEATURE_TRACKING_TITLE"] = getUIString("FEATURE_TRACKING_TITLE");
        strings["FEATURE_TRACKING_DESC"] = getUIString("FEATURE_TRACKING_DESC");
        strings["FEATURE_SYNC_TITLE"] = getUIString("FEATURE_SYNC_TITLE");
        strings["FEATURE_SYNC_DESC"] = getUIString("FEATURE_SYNC_DESC");
        strings["FEATURE_WEIGH_TITLE"] = getUIString("FEATURE_WEIGH_TITLE");
        strings["FEATURE_WEIGH_DESC"] = getUIString("FEATURE_WEIGH_DESC");
        strings["BTN_GET_STARTED"] = getUIString("BTN_GET_STARTED");
        
        // setup.html
        strings["SETUP_TITLE"] = getUIString("SETUP_TITLE");
        strings["REGISTER_TITLE"] = getUIString("REGISTER_TITLE");
        strings["REGISTER_URL_LABEL"] = getUIString("REGISTER_URL_LABEL");
        strings["REGISTER_CODE_LABEL"] = getUIString("REGISTER_CODE_LABEL");
        strings["REGISTER_BTN"] = getUIString("REGISTER_BTN");
        strings["DISPLAY_TITLE"] = getUIString("DISPLAY_TITLE");
        strings["DISPLAY_LANG_LABEL"] = getUIString("DISPLAY_LANG_LABEL");
        strings["DISPLAY_SLEEP_LABEL"] = getUIString("DISPLAY_SLEEP_LABEL");
        strings["DISPLAY_SECONDS_LABEL"] = getUIString("DISPLAY_SECONDS_LABEL");
        strings["REGISTER_ERROR_1"] = getUIString("REGISTER_ERROR_1");
        strings["REGISTER_PROGRESS"] = getUIString("REGISTER_PROGRESS");
        strings["REGISTER_SUCCESS"] = getUIString("REGISTER_SUCCESS");
        strings["REGISTER_FAILED"] = getUIString("REGISTER_FAILED");
        strings["SETTING_SAVED"] = getUIString("SETTING_SAVED");
        strings["SETTING_ERROR"] = getUIString("SETTING_ERROR");
        
        // waage.html
        strings["SCALE_TITLE"] = getUIString("SCALE_TITLE");
        strings["SCALE_ACTIONS_TITLE"] = getUIString("SCALE_ACTIONS_TITLE");
        strings["SCALE_TARE_BTN"] = getUIString("SCALE_TARE_BTN");
        strings["SCALE_CAL_BTN"] = getUIString("SCALE_CAL_BTN");
        strings["SCALE_WS_CONNECTED"] = getUIString("SCALE_WS_CONNECTED");
        strings["SCALE_WS_DISCONNECTED"] = getUIString("SCALE_WS_DISCONNECTED");
        strings["ACTION_SUCCESS"] = getUIString("ACTION_SUCCESS");
        strings["ACTION_ERROR"] = getUIString("ACTION_ERROR");
        
        // upgrade.html
        strings["UPGRADE_TITLE"] = getUIString("UPGRADE_TITLE");
        strings["UPGRADE_FILE_LABEL"] = getUIString("UPGRADE_FILE_LABEL");
        strings["UPGRADE_CHOOSE_BTN"] = getUIString("UPGRADE_CHOOSE_BTN");
        strings["UPGRADE_NO_FILE"] = getUIString("UPGRADE_NO_FILE");
        strings["UPGRADE_BTN"] = getUIString("UPGRADE_BTN");
        strings["UPGRADE_FAIL_PREFIX"] = getUIString("UPGRADE_FAIL_PREFIX");
        
        // wifi.html
        strings["WIFI_TITLE"] = getUIString("WIFI_TITLE");
        strings["WIFI_DESC"] = getUIString("WIFI_DESC");
        strings["WIFI_SSID_LABEL"] = getUIString("WIFI_SSID_LABEL");
        strings["WIFI_NOT_CONNECTED"] = getUIString("WIFI_NOT_CONNECTED");
        strings["WIFI_CONNECT_BTN"] = getUIString("WIFI_CONNECT_BTN");
        strings["WIFI_DISCONNECT_BTN"] = getUIString("WIFI_DISCONNECT_BTN");
        strings["WIFI_SCAN_BTN"] = getUIString("WIFI_SCAN_BTN");
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
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
