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

struct ApiRequest {
    FilamanApiRequestType type;
    int id1;
    int id2;
    String str1;
    String str2;
    float val;
    bool bool1; // success
    String str3; // error message
    bool active = false;
};

#define MAX_API_QUEUE 10
ApiRequest apiQueue[MAX_API_QUEUE];
SemaphoreHandle_t queueMutex;

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
    http.setTimeout(5000);
    http.begin(filamanUrl + "/api/v1/devices/register");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Code", deviceCode);
    int httpCode = http.POST("{}");
    if (httpCode == 200 || httpCode == 201) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            if (doc["token"].is<String>()) {
                filamanToken = doc["token"].as<String>();
                filamanRegistered = true;
                saveFilamanConfig();
                http.end();
                return true;
            }
        }
    }
    http.end();
    return false;
}

bool sendHeartbeat() {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(3000);
    http.begin(filamanUrl + "/api/v1/devices/heartbeat");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    JsonDocument doc;
    doc["ip_address"] = WiFi.localIP().toString();
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    filamanConnected = (httpCode == 200);
    http.end();
    return filamanConnected;
}

bool sendWeight(int spoolId, String tagUuid, float measuredWeight) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(3000);
    http.begin(filamanUrl + "/api/v1/devices/scale/weight");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    JsonDocument doc;
    if (spoolId > 0) doc["spool_id"] = spoolId;
    if (tagUuid.length() > 0) doc["tag_uuid"] = tagUuid;
    doc["measured_weight_g"] = measuredWeight;
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    
    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);
        if (!error && responseDoc["remaining_weight_g"].is<float>()) {
            int remaining = (int)responseDoc["remaining_weight_g"].as<float>();
            pauseMainTask = 1;
            oledShowRemainingWeight(remaining);
            vTaskDelay(pdMS_TO_TICKS(3000));
            pauseMainTask = 0;
        }
        http.end();
        return true;
    }
    else {
        pauseMainTask = 1;
        oledShowProgressBar(1, 1, "Failure", "API Error");
        vTaskDelay(pdMS_TO_TICKS(2000));
        pauseMainTask = 0;
    }
    
    http.end();
    return false;
}

bool sendLocation(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(3000);
    http.begin(filamanUrl + "/api/v1/devices/scale/locate");
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
    http.end();
    return (httpCode == 200);
}

bool sendRfidResult(String tagUuid, int spoolId, int locationId, bool success, String errorMessage) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(5000);
    http.begin(filamanUrl + "/api/v1/devices/rfid-result");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    
    JsonDocument doc;
    doc["success"] = success;
    if (tagUuid.length() > 0) doc["tag_uuid"] = tagUuid;
    if (spoolId > 0) doc["spool_id"] = spoolId;
    if (locationId > 0) doc["location_id"] = locationId;
    if (errorMessage.length() > 0) doc["error_message"] = errorMessage;
    
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    http.end();
    return (httpCode == 200);
}

void filamanApiTask(void* pvParameters) {
    for (;;) {
        ApiRequest req;
        bool hasReq = false;
        
        if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for(int i=0; i<MAX_API_QUEUE; i++) {
                if(apiQueue[i].active) {
                    req = apiQueue[i];
                    apiQueue[i].active = false;
                    hasReq = true;
                    break;
                }
            }
            xSemaphoreGive(queueMutex);
        }

        if (hasReq) {
            filamanApiState = API_TRANSMITTING;
            switch (req.type) {
                case API_REQUEST_HEARTBEAT: sendHeartbeat(); break;
                case API_REQUEST_WEIGHT: sendWeight(req.id1, req.str1, req.val); break;
                case API_REQUEST_LOCATE: sendLocation(req.id1, req.str1, req.id2, req.str2); break;
                case API_REQUEST_RFID_RESULT: sendRfidResult(req.str1, req.id1, req.id2, req.bool1, req.str3); break;
                default: break;
            }
            filamanApiState = API_IDLE;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void sendHeartbeatAsync() {
    if (!checkFilamanRegistration()) return;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Prevent duplicate heartbeats
        for(int i=0; i<MAX_API_QUEUE; i++) if(apiQueue[i].active && apiQueue[i].type == API_REQUEST_HEARTBEAT) {
            xSemaphoreGive(queueMutex);
            return;
        }
        for(int i=0; i<MAX_API_QUEUE; i++) if(!apiQueue[i].active) {
            apiQueue[i].type = API_REQUEST_HEARTBEAT;
            apiQueue[i].id1 = 0;
            apiQueue[i].id2 = 0;
            apiQueue[i].str1 = "";
            apiQueue[i].str2 = "";
            apiQueue[i].val = 0.0f;
            apiQueue[i].active = true;
            break;
        }
        xSemaphoreGive(queueMutex);
    }
}

void sendWeightAsync(int spoolId, String tagUuid, float weight) {
    if (!checkFilamanRegistration()) return;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for(int i=0; i<MAX_API_QUEUE; i++) if(!apiQueue[i].active) {
            apiQueue[i].type = API_REQUEST_WEIGHT;
            apiQueue[i].id1 = spoolId;
            apiQueue[i].id2 = 0;
            apiQueue[i].str1 = tagUuid;
            apiQueue[i].str2 = "";
            apiQueue[i].val = weight;
            apiQueue[i].active = true;
            break;
        }
        xSemaphoreGive(queueMutex);
    }
}

void sendLocationAsync(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid) {
    if (!checkFilamanRegistration()) return;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for(int i=0; i<MAX_API_QUEUE; i++) if(!apiQueue[i].active) {
            apiQueue[i].type = API_REQUEST_LOCATE;
            apiQueue[i].id1 = spoolId;
            apiQueue[i].id2 = locationId;
            apiQueue[i].str1 = spoolTagUuid;
            apiQueue[i].str2 = locationTagUuid;
            apiQueue[i].val = 0.0f;
            apiQueue[i].active = true;
            break;
        }
        xSemaphoreGive(queueMutex);
    }
}

void sendRfidResultAsync(String tagUuid, int spoolId, int locationId, bool success, String errorMessage) {
    if (!checkFilamanRegistration()) return;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for(int i=0; i<MAX_API_QUEUE; i++) if(!apiQueue[i].active) {
            apiQueue[i].type = API_REQUEST_RFID_RESULT;
            apiQueue[i].str1 = tagUuid;
            apiQueue[i].id1 = spoolId;
            apiQueue[i].id2 = locationId;
            apiQueue[i].bool1 = success;
            apiQueue[i].str3 = errorMessage;
            apiQueue[i].active = true;
            break;
        }
        xSemaphoreGive(queueMutex);
    }
}

bool initFilaman() {
    loadFilamanConfig();
    queueMutex = xSemaphoreCreateMutex();
    // Move to Core 1 (Hardware Core) to free up Core 0 for WiFi/Webserver
    // Set priority to 1 (same as Scale/NFC) to ensure fair scheduling
    xTaskCreatePinnedToCore(filamanApiTask, "FilaManApi", 6144, NULL, 1, NULL, 1); 
    if (checkFilamanRegistration()) sendHeartbeatAsync();
    return true;
}
