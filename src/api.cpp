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
#include "display.h"
#include "lang.h"
#include "server_ota.h"

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
    float remainingWeight; // remaining weight from rfid-result
    bool active = false;
    uint32_t seq = 0; // fortlaufende Nummer fuer FIFO-Reihenfolge
};

#define MAX_API_QUEUE 10
ApiRequest apiQueue[MAX_API_QUEUE];
SemaphoreHandle_t queueMutex;
static uint32_t apiQueueSeq = 0;

// Reiht einen Request ein (setzt ALLE Felder). false, wenn Queue voll oder Mutex nicht verfuegbar.
static bool enqueueRequest(const ApiRequest& r) {
    bool queued = false;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < MAX_API_QUEUE; i++) {
            if (!apiQueue[i].active) {
                apiQueue[i] = r;
                apiQueue[i].seq = ++apiQueueSeq;
                apiQueue[i].active = true;
                queued = true;
                break;
            }
        }
        xSemaphoreGive(queueMutex);
    }
    if (!queued) {
        Serial.printf("API queue full, request dropped: type=%d\n", (int)r.type);
    }
    return queued;
}

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
    http.setTimeout(10000);  // 10s statt 3s - wichtig für instabile Verbindungen
    http.setReuse(false);
    http.begin(filamanUrl + "/api/v1/devices/heartbeat");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    JsonDocument doc;
    doc["ip_address"] = WiFi.localIP().toString();
    doc["firmware_version"] = VERSION;
    doc["firmware_project"] = FW_PROJECT;
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    filamanConnected = (httpCode == 200);
    if (httpCode >= 200 && httpCode < 300) {
        serverOtaConfirm();
        // Antwort kann {"firmware_update":"<version>"} enthalten: Server hat andere Firmware bereit.
        String body = http.getString();
        JsonDocument resp;
        if (!deserializeJson(resp, body)) {
            const char* fw = resp["firmware_update"] | "";
            if (fw[0] != '\0') serverOtaUpdateHint(fw);
        }
    }
    http.end();
    return filamanConnected;
}

// Heartbeat mit Retry-Logik für mehr Stabilität
bool sendHeartbeatWithRetry(int maxRetries = 2) {
    for (int attempt = 0; attempt <= maxRetries; attempt++) {
        if (sendHeartbeat()) {
            return true;
        }
        if (attempt < maxRetries) {
            Serial.printf("Heartbeat failed, retry %d/%d...\n", attempt + 1, maxRetries);
            vTaskDelay(pdMS_TO_TICKS(1000));  // 1s warten vor Retry
        }
    }
    Serial.println("Heartbeat failed after all retries");
    return false;
}

bool sendWeight(int spoolId, String tagUuid, float measuredWeight) {
    Serial.printf("sendWeight: sending to API - spoolId=%d, tagUuid=%s, weight=%.1f\n", spoolId, tagUuid.c_str(), measuredWeight);
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: Not registered or WiFi not connected");
        return false;
    }
    HTTPClient http;
    http.setTimeout(10000);  // 10s Timeout
    http.setReuse(false);
    http.begin(filamanUrl + "/api/v1/devices/scale/weight");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    JsonDocument doc;
    // Only add spool_id if it's > 0 (for NTAG tags with spool ID)
    // For Bambu tags (spoolId == 0), only send tag_uuid
    if (spoolId > 0) doc["spool_id"] = spoolId;
    // Always add tag_uuid if available (this is what we want for Bambu tags)
    if (tagUuid.length() > 0) doc["tag_uuid"] = tagUuid;
    doc["measured_weight_g"] = measuredWeight;
    String payload;
    serializeJson(doc, payload);
    Serial.printf("API payload: %s\n", payload.c_str());
    int httpCode = http.POST(payload);
    Serial.printf("API response code: %d\n", httpCode);

    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);
        if (!error && responseDoc["remaining_weight_g"].is<float>()) {
            int remaining = (int)responseDoc["remaining_weight_g"].as<float>();
            oledShowRemainingWeight(remaining);
            oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);
            vTaskDelay(pdMS_TO_TICKS(3000));
            oledClearPriority();
        }
        http.end();
        return true;
    }
    else {
        oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_API_ERROR));
        oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
        vTaskDelay(pdMS_TO_TICKS(2000));
        oledClearPriority();
    }

    http.end();
    return false;
}

bool sendLocation(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(10000);  // 10s Timeout
    http.setReuse(false);
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

bool sendRfidResult(String tagUuid, int spoolId, int locationId, bool success, String errorMessage, float remainingWeight) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(10000);  // 10s Timeout
    http.setReuse(false);
    http.begin(filamanUrl + "/api/v1/devices/rfid-result");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);

    JsonDocument doc;
    doc["success"] = success;
    if (tagUuid.length() > 0) doc["tag_uuid"] = tagUuid;
    if (spoolId > 0) doc["spool_id"] = spoolId;
    if (locationId > 0) doc["location_id"] = locationId;
    if (errorMessage.length() > 0) doc["error_message"] = errorMessage;
    if (remainingWeight > 0) doc["remaining_weight_g"] = remainingWeight;

    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    http.end();
    return (httpCode == 200);
}

bool sendTagData(String tagJson) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(10000);
    http.setReuse(false);
    http.begin(filamanUrl + "/api/v1/devices/tag-data");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    JsonDocument doc;
    doc["tag_json"] = tagJson;
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    http.end();
    Serial.printf("sendTagData: HTTP %d\n", httpCode);
    return (httpCode == 200);
}

void filamanApiTask(void* pvParameters) {
    for (;;) {
        // Waehrend eines Firmware-Updates keine Queue-Requests (Berichte sendet der OTA-Task selbst).
        if (firmwareUpdateActive()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        ApiRequest req;
        bool hasReq = false;

        if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int best = -1;
            for(int i=0; i<MAX_API_QUEUE; i++) {
                if(apiQueue[i].active && (best < 0 || (int32_t)(apiQueue[i].seq - apiQueue[best].seq) < 0)) {
                    best = i;
                }
            }
            if (best >= 0) {
                req = apiQueue[best];
                apiQueue[best].active = false;
                // Strings freigeben, damit grosse Tag-JSONs nicht dauerhaft Heap belegen
                apiQueue[best].str1 = String();
                apiQueue[best].str2 = String();
                apiQueue[best].str3 = String();
                hasReq = true;
            }
            xSemaphoreGive(queueMutex);
        }

        if (hasReq) {
            filamanApiState = API_TRANSMITTING;
            switch (req.type) {
                case API_REQUEST_REGISTER: {
                    // URL übernehmen und persistieren, dann blockierend
                    // registrieren - hier unkritisch, da eigener Task.
                    if (req.str2.length() > 0) {
                        String u = req.str2;
                        u.trim();
                        while (u.endsWith("/")) u.remove(u.length() - 1);
                        filamanUrl = u;
                    }
                    saveFilamanConfig();
                    bool ok = registerDevice(req.str1);
                    Serial.printf("registerDevice: %s\n", ok ? "success" : "failed");
                    sendRegisterResult(ok);
                    break;
                }
                case API_REQUEST_HEARTBEAT: sendHeartbeatWithRetry(2); break;
                case API_REQUEST_WEIGHT: sendWeight(req.id1, req.str1, req.val); break;
                case API_REQUEST_LOCATE: sendLocation(req.id1, req.str1, req.id2, req.str2); break;
                case API_REQUEST_RFID_RESULT: sendRfidResult(req.str1, req.id1, req.id2, req.bool1, req.str3, req.remainingWeight); break;
                case API_REQUEST_TAG_DATA: sendTagData(req.str1); break;
                default: break;
            }
            filamanApiState = API_IDLE;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Neuen, vollstaendig initialisierten Request anlegen.
static ApiRequest makeRequest(FilamanApiRequestType type) {
    ApiRequest r;
    r.type = type;
    r.id1 = 0;
    r.id2 = 0;
    r.val = 0.0f;
    r.bool1 = false;
    r.remainingWeight = 0.0f;
    return r;
}

// Reiht die Registrierung ein, statt sie im AsyncTCP-Kontext auszuführen.
// str2 = FilaMan-URL, str1 = Device-Code.
// Ohne Queue-Platz kaeme nie ein "registerResult" - der Aufrufer muss
// dem Frontend dann sofort einen Fehler zurueckgeben (Rueckgabe false).
bool registerDeviceAsync(const String& url, const String& deviceCode) {
    ApiRequest r = makeRequest(API_REQUEST_REGISTER);
    r.str1 = deviceCode;
    r.str2 = url;
    return enqueueRequest(r);
}

void sendHeartbeatAsync() {
    if (!checkFilamanRegistration()) return;
    // Prevent duplicate heartbeats
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for(int i=0; i<MAX_API_QUEUE; i++) if(apiQueue[i].active && apiQueue[i].type == API_REQUEST_HEARTBEAT) {
            xSemaphoreGive(queueMutex);
            return;
        }
        xSemaphoreGive(queueMutex);
    }
    enqueueRequest(makeRequest(API_REQUEST_HEARTBEAT));
}

void sendWeightAsync(int spoolId, String tagUuid, float weight) {
    Serial.printf("sendWeightAsync: spoolId=%d, tagUuid=%s, weight=%.1f\n", spoolId, tagUuid.c_str(), weight);
    if (!checkFilamanRegistration()) {
        Serial.println("ERROR: Not registered with FilaMan, cannot send weight");
        return;
    }
    if (weight <= 0) {
        Serial.println("ERROR: Weight is 0 or negative, cannot send");
        return;
    }
    ApiRequest r = makeRequest(API_REQUEST_WEIGHT);
    r.id1 = spoolId;
    r.str1 = tagUuid;
    r.val = weight;
    if (enqueueRequest(r)) Serial.println("Weight queued for API");
}

void sendLocationAsync(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid) {
    if (!checkFilamanRegistration()) return;
    ApiRequest r = makeRequest(API_REQUEST_LOCATE);
    r.id1 = spoolId;
    r.id2 = locationId;
    r.str1 = spoolTagUuid;
    r.str2 = locationTagUuid;
    enqueueRequest(r);
}

void sendRfidResultAsync(String tagUuid, int spoolId, int locationId, bool success, String errorMessage, float remainingWeight) {
    if (!checkFilamanRegistration()) return;
    ApiRequest r = makeRequest(API_REQUEST_RFID_RESULT);
    r.str1 = tagUuid;
    r.id1 = spoolId;
    r.id2 = locationId;
    r.bool1 = success;
    r.str3 = errorMessage;
    r.remainingWeight = remainingWeight;
    enqueueRequest(r);
}

void sendTagDataAsync(String tagJson) {
    if (!checkFilamanRegistration()) return;
    ApiRequest r = makeRequest(API_REQUEST_TAG_DATA);
    r.str1 = tagJson;
    enqueueRequest(r);
}

bool initFilaman() {
    oledShowProgressBar(3, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_API_INIT));
    loadFilamanConfig();
    queueMutex = xSemaphoreCreateMutex();
    // Move to Core 1 (Hardware Core) to free up Core 0 for WiFi/Webserver
    // Set priority to 1 (same as Scale/NFC) to ensure fair scheduling
    xTaskCreatePinnedToCore(filamanApiTask, "FilaManApi", 8192, NULL, 1, NULL, 1);
    if (checkFilamanRegistration()) sendHeartbeatAsync();
    return true;
}
