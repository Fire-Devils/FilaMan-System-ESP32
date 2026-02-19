#include "api.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "commonFS.h"
#include <Preferences.h>
#include "debug.h"
#include "scale.h"
#include "nfc.h"
#include "config.h"
#include <WiFi.h>

volatile filamanApiStateType filamanApiState = API_IDLE;
bool filamanConnected = false;

void saveFilamanConfig() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, false);
    preferences.putString(NVS_KEY_FILAMAN_URL, filamanUrl);
    preferences.putString(NVS_KEY_FILAMAN_TOKEN, filamanToken);
    preferences.putBool(NVS_KEY_FILAMAN_REGISTERED, filamanRegistered);
    preferences.end();
}

void loadFilamanConfig() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, true);
    filamanUrl = preferences.getString(NVS_KEY_FILAMAN_URL, "");
    filamanToken = preferences.getString(NVS_KEY_FILAMAN_TOKEN, "");
    filamanRegistered = preferences.getBool(NVS_KEY_FILAMAN_REGISTERED, false);
    preferences.end();
}

bool checkFilamanRegistration() {
    return filamanRegistered && filamanToken.length() > 0;
}

bool registerDevice(const String& deviceCode) {
    if (filamanUrl.length() == 0) return false;

    HTTPClient http;
    String url = filamanUrl + "/api/v1/devices/register";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Code", deviceCode);
    
    int httpCode = http.POST("{}");
    bool success = false;

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error && doc["token"].is<String>()) {
            filamanToken = doc["token"].as<String>();
            filamanRegistered = true;
            saveFilamanConfig();
            success = true;
            Serial.println("Device successfully registered!");
        }
    } else {
        Serial.printf("Registration failed, HTTP code: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

bool sendHeartbeat() {
    if (!checkFilamanRegistration()) return false;

    HTTPClient http;
    String url = filamanUrl + "/api/v1/devices/heartbeat";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    
    JsonDocument doc;
    doc["ip_address"] = WiFi.localIP().toString();
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == HTTP_CODE_OK);
    
    if (success) {
        filamanConnected = true;
    } else {
        Serial.printf("Heartbeat failed, HTTP code: %d\n", httpCode);
        if (httpCode == 401) {
            filamanRegistered = false; // Token might be invalid
            saveFilamanConfig();
        }
        filamanConnected = false;
    }
    
    http.end();
    return success;
}

bool sendWeight(int spoolId, String tagUuid, float measuredWeight) {
    if (!checkFilamanRegistration()) return false;

    HTTPClient http;
    String url = filamanUrl + "/api/v1/devices/scale/weight";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    
    JsonDocument doc;
    if (spoolId > 0) doc["spool_id"] = spoolId;
    if (tagUuid.length() > 0) doc["tag_uuid"] = tagUuid;
    doc["measured_weight_g"] = measuredWeight;
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == HTTP_CODE_OK);
    
    if (!success) {
        Serial.printf("Weight update failed, HTTP code: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

bool sendLocation(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid) {
    if (!checkFilamanRegistration()) return false;

    HTTPClient http;
    String url = filamanUrl + "/api/v1/devices/scale/locate";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    
    JsonDocument doc;
    if (spoolId > 0) doc["spool_id"] = spoolId;
    if (spoolTagUuid.length() > 0) doc["spool_tag_uuid"] = spoolTagUuid;
    if (locationId > 0) doc["location_id"] = locationId;
    if (locationTagUuid.length() > 0) doc["location_tag_uuid"] = locationTagUuid;
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == HTTP_CODE_OK);
    
    if (!success) {
        Serial.printf("Location update failed, HTTP code: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

bool initFilaman() {
    oledShowProgressBar(3, 7, DISPLAY_BOOT_TEXT, "FilaMan init");
    loadFilamanConfig();
    
    if (checkFilamanRegistration()) {
        sendHeartbeat();
    }
    
    oledShowTopRow();
    return true;
}
