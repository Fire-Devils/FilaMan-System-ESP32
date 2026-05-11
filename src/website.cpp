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

    // Navigation
    if (strcmp(key, "NAV_HOME") == 0) {
        return (lang == LANG_DE) ? "Start" : (lang == LANG_ZH) ? "首页" : "Home";
    }
    if (strcmp(key, "NAV_SCALE") == 0) {
        return (lang == LANG_DE) ? "Waage" : (lang == LANG_ZH) ? "称重" : "Scale";
    }
    if (strcmp(key, "NAV_SETUP") == 0) {
        return (lang == LANG_DE) ? "Setup" : (lang == LANG_ZH) ? "设置" : "Setup";
    }
    if (strcmp(key, "NAV_UPGRADE") == 0) {
        return (lang == LANG_DE) ? "Upgrade" : (lang == LANG_ZH) ? "升级" : "Upgrade";
    }

    // Common UI
    if (strcmp(key, "SYSTEM_SYNC") == 0) {
        return (lang == LANG_DE) ? "System Sync" : (lang == LANG_ZH) ? "系统同步" : "System Sync";
    }
    if (strcmp(key, "VERSION_LABEL") == 0) {
        return (lang == LANG_DE) ? "" : (lang == LANG_ZH) ? "" : "v";
    }

    // index.html
    if (strcmp(key, "WELCOME_TITLE") == 0) {
        return (lang == LANG_DE) ? "Willkommen bei FilaMan" : (lang == LANG_ZH) ? "欢迎使用 FilaMan" : "Welcome to FilaMan";
    }
    if (strcmp(key, "WELCOME_SUBTITLE_1") == 0) {
        return (lang == LANG_DE) ? "Ihre smarte Lösung für" : (lang == LANG_ZH) ? "您的智能" : "Your smart solution for";
    }
    if (strcmp(key, "WELCOME_SUBTITLE_2") == 0) {
        return (lang == LANG_DE) ? "Filament-Management" : (lang == LANG_ZH) ? "丝材管理" : "Filament Management";
    }
    if (strcmp(key, "WELCOME_SUBTITLE_3") == 0) {
        return (lang == LANG_DE) ? "im 3D-Druck." : (lang == LANG_ZH) ? "3D打印解决方案" : "in 3D printing.";
    }
    if (strcmp(key, "CARD_COMPAT_TITLE") == 0) {
        return (lang == LANG_DE) ? "System-Kompatibilität" : (lang == LANG_ZH) ? "系统兼容性" : "System Compatibility";
    }
    if (strcmp(key, "COMPAT_TEXT_1") == 0) {
        return (lang == LANG_DE) ? "Ab" : (lang == LANG_ZH) ? "始于" : "Starting with";
    }
    if (strcmp(key, "COMPAT_TEXT_2") == 0) {
        return (lang == LANG_DE) ? "Version 3.0.0" : (lang == LANG_ZH) ? "3.0.0 版本" : "Version 3.0.0";
    }
    if (strcmp(key, "COMPAT_TEXT_3") == 0) {
        return (lang == LANG_DE) ? "mit ESP32" : (lang == LANG_ZH) ? "基于 ESP32" : "with ESP32";
    }
    if (strcmp(key, "COMPAT_TEXT_4") == 0) {
        return (lang == LANG_DE) ? "& OLED Display" : (lang == LANG_ZH) ? "与 OLED 显示屏" : "& OLED Display";
    }
    if (strcmp(key, "FEATURE_TRACKING_TITLE") == 0) {
        return (lang == LANG_DE) ? "Präzise Rückverfolgung" : (lang == LANG_ZH) ? "精准追踪" : "Precise Tracking";
    }
    if (strcmp(key, "FEATURE_TRACKING_DESC") == 0) {
        return (lang == LANG_DE) ? "Nahtlose Aktualisierung der Spulendaten über die FilaMan-API für präzise Rückverfolgung." : (lang == LANG_ZH) ? "通过 FilaMan API 无缝更新丝材数据，实现精准追踪。" : "Seamlessly update spool data via FilaMan API for precise tracking.";
    }
    if (strcmp(key, "FEATURE_SYNC_TITLE") == 0) {
        return (lang == LANG_DE) ? "Automatische Synchronisation" : (lang == LANG_ZH) ? "自动同步" : "Automatic Sync";
    }
    if (strcmp(key, "FEATURE_SYNC_DESC") == 0) {
        return (lang == LANG_DE) ? "Spulendaten automatisch mit der Cloud synchronisieren, um den Bestand immer aktuell zu halten." : (lang == LANG_ZH) ? "自动与云端同步丝材数据，保持库存始终最新。" : "Automatically sync spool data with cloud to keep inventory up-to-date.";
    }
    if (strcmp(key, "FEATURE_WEIGH_TITLE") == 0) {
        return (lang == LANG_DE) ? "Präzises Wiegen" : (lang == LANG_ZH) ? "精准称重" : "Precise Weighing";
    }
    if (strcmp(key, "FEATURE_WEIGH_DESC") == 0) {
        return (lang == LANG_DE) ? "HX711-Sensor für genaue Gewichtsmessung der Spulen." : (lang == LANG_ZH) ? "HX711 传感器，精确测量丝材重量。" : "HX711 sensor for accurate spool weight measurement.";
    }
    if (strcmp(key, "BTN_GET_STARTED") == 0) {
        return (lang == LANG_DE) ? "Loslegen" : (lang == LANG_ZH) ? "开始使用" : "Get Started";
    }

    // setup.html
    if (strcmp(key, "SETUP_TITLE") == 0) {
        return (lang == LANG_DE) ? "Setup" : (lang == LANG_ZH) ? "系统设置" : "Setup";
    }
    if (strcmp(key, "REGISTER_TITLE") == 0) {
        return (lang == LANG_DE) ? "Gerät registrieren" : (lang == LANG_ZH) ? "注册设备" : "Register Device";
    }
    if (strcmp(key, "REGISTER_URL_LABEL") == 0) {
        return (lang == LANG_DE) ? "FilaMan URL" : (lang == LANG_ZH) ? "FilaMan 网址" : "FilaMan URL";
    }
    if (strcmp(key, "REGISTER_CODE_LABEL") == 0) {
        return (lang == LANG_DE) ? "Registrierungscode" : (lang == LANG_ZH) ? "注册码" : "Registration Code";
    }
    if (strcmp(key, "REGISTER_BTN") == 0) {
        return (lang == LANG_DE) ? "Registrieren" : (lang == LANG_ZH) ? "注册" : "Register";
    }
    if (strcmp(key, "DEVICE_ID_LABEL") == 0) {
        return (lang == LANG_DE) ? "Geräte-ID" : (lang == LANG_ZH) ? "设备 ID" : "Device ID";
    }
    if (strcmp(key, "DEVICE_KEY_LABEL") == 0) {
        return (lang == LANG_DE) ? "Geräte-Schlüssel" : (lang == LANG_ZH) ? "设备密钥" : "Device Key";
    }
    if (strcmp(key, "DEVICE_TOKEN_LABEL") == 0) {
        return (lang == LANG_DE) ? "Geräte-Token" : (lang == LANG_ZH) ? "设备令牌" : "Device Token";
    }
    if (strcmp(key, "DISPLAY_LANG_LABEL") == 0) {
        return (lang == LANG_DE) ? "Display-Sprache" : (lang == LANG_ZH) ? "显示语言" : "Display Language";
    }
    if (strcmp(key, "DISPLAY_LANG_OPTION_EN") == 0) {
        return (lang == LANG_DE) ? "Englisch" : (lang == LANG_ZH) ? "英语" : "English";
    }
    if (strcmp(key, "DISPLAY_LANG_OPTION_DE") == 0) {
        return (lang == LANG_DE) ? "Deutsch" : (lang == LANG_ZH) ? "德语" : "German";
    }
    if (strcmp(key, "DISPLAY_LANG_OPTION_ZH") == 0) {
        return (lang == LANG_DE) ? "Chinesisch" : (lang == LANG_ZH) ? "中文" : "Chinese";
    }
    if (strcmp(key, "THEME_LABEL") == 0) {
        return (lang == LANG_DE) ? "Theme" : (lang == LANG_ZH) ? "主题" : "Theme";
    }
    if (strcmp(key, "THEME_LIGHT") == 0) {
        return (lang == LANG_DE) ? "Hell" : (lang == LANG_ZH) ? "浅色" : "Light";
    }
    if (strcmp(key, "THEME_DARK") == 0) {
        return (lang == LANG_DE) ? "Dunkel" : (lang == LANG_ZH) ? "深色" : "Dark";
    }
    if (strcmp(key, "TIMEZONE_LABEL") == 0) {
        return (lang == LANG_DE) ? "Zeitzone" : (lang == LANG_ZH) ? "时区" : "Timezone";
    }
    if (strcmp(key, "TIMEZONE_AUTO") == 0) {
        return (lang == LANG_DE) ? "Automatisch" : (lang == LANG_ZH) ? "自动" : "Automatic";
    }
    if (strcmp(key, "TIMEZONE_MANUAL") == 0) {
        return (lang == LANG_DE) ? "Manuell" : (lang == LANG_ZH) ? "手动" : "Manual";
    }
    if (strcmp(key, "SAVE_BTN") == 0) {
        return (lang == LANG_DE) ? "Speichern" : (lang == LANG_ZH) ? "保存" : "Save";
    }
    if (strcmp(key, "CANCEL_BTN") == 0) {
        return (lang == LANG_DE) ? "Abbrechen" : (lang == LANG_ZH) ? "取消" : "Cancel";
    }

    // waage.html
    if (strcmp(key, "WAAGE_TITLE") == 0) {
        return (lang == LANG_DE) ? "Waage" : (lang == LANG_ZH) ? "称重" : "Scale";
    }
    if (strcmp(key, "WEIGHT_LABEL") == 0) {
        return (lang == LANG_DE) ? "Gewicht" : (lang == LANG_ZH) ? "重量" : "Weight";
    }
    if (strcmp(key, "TARE_BTN") == 0) {
        return (lang == LANG_DE) ? "Tarieren" : (lang == LANG_ZH) ? "去皮" : "Tare";
    }
    if (strcmp(key, "CALIBRATE_BTN") == 0) {
        return (lang == LANG_DE) ? "Kalibrieren" : (lang == LANG_ZH) ? "校准" : "Calibrate";
    }

    // wifi.html
    if (strcmp(key, "WIFI_TITLE") == 0) {
        return (lang == LANG_DE) ? "Wi-Fi" : (lang == LANG_ZH) ? "无线网络" : "Wi-Fi";
    }
    if (strcmp(key, "WIFI_SCAN") == 0) {
        return (lang == LANG_DE) ? "Netzwerke scannen" : (lang == LANG_ZH) ? "扫描网络" : "Scan Networks";
    }

    // upgrade.html
    if (strcmp(key, "UPGRADE_TITLE") == 0) {
        return (lang == LANG_DE) ? "Firmware Update" : (lang == LANG_ZH) ? "固件更新" : "Firmware Update";
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
