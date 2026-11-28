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

void saveFilamanConfig() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, false);
    preferences.putString(NVS_KEY_FILAMAN_URL, filamanUrl);
    preferences.putString(NVS_KEY_FILAMAN_TOKEN, filamanToken);
    preferences.putBool(NVS_KEY_FILAMAN_REGISTERED, filamanRegistered);
    preferences.putString(NVS_KEY_SPOOLMAN_URL, spoolmanUrl);
    preferences.end();
}

void loadFilamanConfig() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, true);
    filamanUrl = preferences.getString(NVS_KEY_FILAMAN_URL, "");
    filamanToken = preferences.getString(NVS_KEY_FILAMAN_TOKEN, "");
    filamanRegistered = preferences.getBool(NVS_KEY_FILAMAN_REGISTERED, false);
    spoolmanUrl = preferences.getString(NVS_KEY_SPOOLMAN_URL, "");
    preferences.end();
}

bool checkFilamanRegistration() {
    if (!spoolmanUrl.isEmpty()) return true;  // Spoolman braucht keine Registrierung
    return filamanRegistered && filamanToken.length() > 0;
}

// Spoolman-Backend (Implementierung weiter unten)
static bool spoolmanHeartbeat();
static bool spoolmanWeight(int spoolId, const String& uid, float weight);
static bool spoolmanSpoolFull(int spoolId, const String& uid, float weight);
static bool spoolmanLocation(int spoolId, const String& uid, int locationId, const String& locationName);
static bool spoolmanRfidResult(const String& uid, int spoolId, bool success);

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
    if (!spoolmanUrl.isEmpty()) return spoolmanHeartbeat();
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
    if (!spoolmanUrl.isEmpty()) return spoolmanWeight(spoolId, tagUuid, measuredWeight);
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
            lastRemainingShown = (uint16_t)remaining;
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

volatile bool spoolFullGestureEnabled = false;
volatile bool spoolFullOverwritePending = false;
volatile unsigned long spoolFullOverwriteUntil = 0;
volatile int spoolFullLastHttpCode = 0;
volatile unsigned long spoolFullLastAt = 0;
volatile int spoolFullLastOldTara = -1;
volatile int spoolFullLastNewTara = -1;
static int sfSpoolId = 0;
static String sfTagUuid;
static float sfWeight = 0;

void loadSpoolFullGesture() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, true);
    spoolFullGestureEnabled = preferences.getBool(NVS_KEY_SPOOL_FULL_GESTURE, false);
    preferences.end();
}

void saveSpoolFullGesture(bool enabled) {
    spoolFullGestureEnabled = enabled;
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, false);
    preferences.putBool(NVS_KEY_SPOOL_FULL_GESTURE, enabled);
    preferences.end();
}

// Kurze OLED-Meldung aus dem API-Task (nicht blockierend, Prioritaet laeuft von selbst ab)
volatile bool spoolFullRestPending = false;
volatile uint16_t spoolFullRestWeight = 0;
volatile uint16_t lastRemainingShown = 0;
volatile unsigned long spoolFullRestAt = 0;

static void spoolFullShow(const String& msg, unsigned long ms, uint8_t size = 1) {
    oledDisplayText(msg, size);
    oledSetPriority(DISPLAY_PRIORITY_WARNING, ms);
}

bool sendSpoolFull(int spoolId, String tagUuid, float weight, bool confirmOverwrite) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        spoolFullShow("No connection", 2500);
        return false;
    }
    if (!spoolmanUrl.isEmpty()) return spoolmanSpoolFull(spoolId, tagUuid, weight);
    sfSpoolId = spoolId; sfTagUuid = tagUuid; sfWeight = weight;
    spoolFullOverwritePending = false;
    spoolFullRestPending = false;
    HTTPClient http;
    http.setTimeout(10000);
    http.setReuse(false);
    http.begin(filamanUrl + "/api/v1/devices/scale/spool-full");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + filamanToken);
    JsonDocument doc;
    if (spoolId > 0) doc["spool_id"] = spoolId;
    if (tagUuid.length() > 0) doc["tag_uuid"] = tagUuid;
    doc["measured_weight_g"] = weight;
    doc["confirm_overwrite"] = confirmOverwrite;
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    Serial.printf("spool-full response code: %d\n", httpCode);
    String body = (httpCode > 0) ? http.getString() : String();
    http.end();

    JsonDocument resp;
    bool parsed = (httpCode > 0) && !deserializeJson(resp, body);

    // Fuer /api/debug-state: Statuscode und Tara-Werte der letzten Antwort merken (nur kopieren)
    spoolFullLastOldTara = -1;
    spoolFullLastNewTara = -1;
    if (parsed) {
        JsonVariant src = (httpCode == 200) ? resp.as<JsonVariant>() : resp["detail"].as<JsonVariant>();
        if (src["old_tara_g"].is<float>()) spoolFullLastOldTara = (int)lroundf(src["old_tara_g"].as<float>());
        if (src["new_tara_g"].is<float>()) spoolFullLastNewTara = (int)lroundf(src["new_tara_g"].as<float>());
    }
    spoolFullLastHttpCode = httpCode;
    spoolFullLastAt = millis();

    if (httpCode == 200 && parsed) {
        String msg = "Tara new: " + String((int)lroundf(resp["new_tara_g"].as<float>())) + " g";
        if (!resp["old_tara_g"].isNull()) msg += "\nold: " + String((int)lroundf(resp["old_tara_g"].as<float>())) + " g";
        bool hasRemaining = resp["remaining_weight_g"].is<float>();
        spoolFullShow(msg, 4000);
        if (hasRemaining) {
            // danach uebernimmt die Main-Loop die "Rest"-Anzeige
            spoolFullRestWeight = (uint16_t)lroundf(resp["remaining_weight_g"].as<float>());
            lastRemainingShown = spoolFullRestWeight;
            spoolFullRestAt = millis() + 4000;
            spoolFullRestPending = true;
        }
        return true;
    }
    if (httpCode == 409 && parsed) {
        JsonObject d = resp["detail"];
        int oldT = (int)lroundf(d["old_tara_g"].as<float>());
        int newT = (int)lroundf(d["new_tara_g"].as<float>());
        oledDisplayText("Tara old " + String(oldT) + " / new " + String(newT) + "\nTap = accept", 1);
        spoolFullOverwriteUntil = millis() + 8000;
        spoolFullOverwritePending = true;
        oledSetPriority(DISPLAY_PRIORITY_WARNING, 8000);
        return false;  // Anzeige bleibt; Main-Loop raeumt nach Ablauf des Fensters auf
    }
    if (httpCode == 422 && parsed) {
        int t = (int)lroundf(resp["detail"]["new_tara_g"].as<float>());
        spoolFullShow("Tara " + String(t) + " g\nout of range", 3500);
        return false;
    }
    if (httpCode == 404) {
        spoolFullShow("Spool/server\nnot found", 3500);
        return false;
    }
    if (httpCode <= 0) spoolFullShow("Network error", 3000);
    else spoolFullShow("API error " + String(httpCode), 3000);
    return false;
}

bool sendLocation(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid, String locationName) {
    if (!spoolmanUrl.isEmpty()) return spoolmanLocation(spoolId, spoolTagUuid, locationId, locationName);
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
    if (!spoolmanUrl.isEmpty()) return spoolmanRfidResult(tagUuid, spoolId, success);
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
    if (!spoolmanUrl.isEmpty()) return false;  // nur FilaMan-Scan-Request
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

// ===================== Spoolman-Backend =====================
// Aktiv, sobald spoolmanUrl gesetzt ist. Spiegelt die FilaMan-Device-Calls auf die Spoolman-REST-API.
// Spulen werden ueber spool_id/sm_id vom Tag gefunden, sonst ueber das Extra-Feld "nfc_id" (Tag-UID).

static int spoolmanHttp(const char* method, const String& path, const String& body, JsonDocument* out = nullptr) {
    if (WiFi.status() != WL_CONNECTED) return -1;
    HTTPClient http;
    http.setTimeout(10000);
    http.setReuse(false);
    http.begin(spoolmanUrl + path);
    if (body.length() > 0) http.addHeader("Content-Type", "application/json");
    int code = http.sendRequest(method, body);
    if (out && code >= 200 && code < 300) {
        String resp = http.getString();
        if (deserializeJson(*out, resp)) code = -2;
    }
    http.end();
    Serial.printf("Spoolman %s %s -> %d\n", method, path.c_str(), code);
    return code;
}

// Extra-Felder fuer Tag-UIDs einmalig anlegen. Werte von Extra-Feldern sind JSON-kodiert.
//  nfc_id    : UID wie die Waage sie liest ("04:a1:..."), kompatibel zu FilaMan v2
//  card_uids : UIDs fuer Snapmaker U1 Extended Firmware / Moonraker spoollink ("04A1...", kommagetrennt)
static bool spoolmanNfcFieldReady = false;
static void spoolmanEnsureNfcField() {
    if (spoolmanNfcFieldReady) return;
    JsonDocument fields;
    if (spoolmanHttp("GET", "/api/v1/field/spool", "", &fields) != 200) return;
    bool hasNfc = false, hasCards = false;
    for (JsonObject f : fields.as<JsonArray>()) {
        if (f["key"] == "nfc_id") hasNfc = true;
        if (f["key"] == "card_uids") hasCards = true;
    }
    bool ok = true;
    if (!hasNfc) ok &= spoolmanHttp("POST", "/api/v1/field/spool/nfc_id", "{\"name\":\"NFC ID\",\"field_type\":\"text\"}") == 200;
    if (!hasCards) ok &= spoolmanHttp("POST", "/api/v1/field/spool/card_uids", "{\"name\":\"Card UIDs\",\"field_type\":\"text\",\"order\":1,\"default_value\":\"\\\"\\\"\"}") == 200;
    spoolmanNfcFieldReady = ok;
}

// Spule ueber extra.nfc_id suchen. 0 = nicht gefunden.
// ponytail: laedt alle Spulen (gefiltert auf id+nfc_id) - reicht fuer einige hundert Spulen, sonst serverseitig filtern.
static int spoolmanFindSpoolByUid(const String& uid) {
    if (uid.isEmpty()) return 0;
    HTTPClient http;
    http.setTimeout(15000);
    http.setReuse(false);
    http.begin(spoolmanUrl + "/api/v1/spool");
    int code = http.GET();
    int found = 0;
    if (code == 200) {
        JsonDocument filter;
        filter[0]["id"] = true;
        filter[0]["extra"]["nfc_id"] = true;
        filter[0]["extra"]["card_uids"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
            String want = "\"" + uid + "\"";
            String card = uid;               // U1-Format: Grossbuchstaben ohne Trenner
            card.replace(":", "");
            card.toUpperCase();
            card = "," + card + ",";
            for (JsonObject sp : doc.as<JsonArray>()) {
                const char* v = sp["extra"]["nfc_id"] | "";
                String cards = sp["extra"]["card_uids"] | "";
                cards.replace("\"", "");
                cards.replace(" ", "");
                cards.toUpperCase();
                if (want.equalsIgnoreCase(v) || ("," + cards + ",").indexOf(card) >= 0) { found = sp["id"] | 0; break; }
            }
        }
    }
    http.end();
    Serial.printf("Spoolman lookup uid=%s -> spool %d\n", uid.c_str(), found);
    return found;
}

static int spoolmanResolve(int spoolId, const String& uid) {
    return spoolId > 0 ? spoolId : spoolmanFindSpoolByUid(uid);
}

static bool spoolmanHeartbeat() {
    JsonDocument doc;
    filamanConnected = (spoolmanHttp("GET", "/api/v1/health", "", &doc) == 200);
    if (filamanConnected) {
        serverOtaConfirm();       // neue Firmware hat Kontakt -> kein Rollback
        spoolmanEnsureNfcField();
    }
    return filamanConnected;
}

static void showApiError() {
    oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_API_ERROR));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    oledClearPriority();
}

static bool spoolmanWeight(int spoolId, const String& uid, float weight) {
    int id = spoolmanResolve(spoolId, uid);
    if (id <= 0) { showApiError(); return false; }
    // /measure: Bruttogewicht, Spoolman zieht das Leergewicht der Spule selbst ab
    JsonDocument resp;
    int code = spoolmanHttp("PUT", "/api/v1/spool/" + String(id) + "/measure", "{\"weight\":" + String(weight, 1) + "}", &resp);
    if (code != 200) { showApiError(); return false; }
    if (resp["remaining_weight"].is<float>()) {
        int remaining = (int)resp["remaining_weight"].as<float>();
        lastRemainingShown = (uint16_t)max(remaining, 0);
        oledShowRemainingWeight(remaining);
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);
        vTaskDelay(pdMS_TO_TICKS(3000));
        oledClearPriority();
    }
    return true;
}

// Spule voll: Leergewicht = aktuelles Gewicht - Filament-Startgewicht, verbrauchtes Gewicht = 0.
// Die Tipp-Bestaetigung an der Waage reicht als Bestaetigung, daher kein 409-Dialog wie bei FilaMan.
static bool spoolmanSpoolFull(int spoolId, const String& uid, float weight) {
    int id = spoolmanResolve(spoolId, uid);
    spoolFullLastOldTara = -1;
    spoolFullLastNewTara = -1;
    spoolFullLastAt = millis();
    if (id <= 0) { spoolFullLastHttpCode = 404; spoolFullShow("Spool/server\nnot found", 3500); return false; }
    JsonDocument sp;
    int code = spoolmanHttp("GET", "/api/v1/spool/" + String(id), "", &sp);
    float initial = sp["initial_weight"].is<float>() ? sp["initial_weight"].as<float>() : (sp["filament"]["weight"] | 0.0f);
    if (code != 200 || initial <= 0) {
        spoolFullLastHttpCode = code;
        spoolFullShow(code == 200 ? "No initial\nweight set" : "API error " + String(code), 3500);
        return false;
    }
    int newT = (int)lroundf(weight - initial);
    if (!sp["spool_weight"].isNull()) spoolFullLastOldTara = (int)lroundf(sp["spool_weight"].as<float>());
    spoolFullLastNewTara = newT;
    if (newT < 0 || newT > 2000) {
        spoolFullLastHttpCode = 422;
        spoolFullShow("Tara " + String(newT) + " g\nout of range", 3500);
        return false;
    }
    code = spoolmanHttp("PATCH", "/api/v1/spool/" + String(id), "{\"spool_weight\":" + String(newT) + ",\"used_weight\":0}");
    spoolFullLastHttpCode = code;
    if (code != 200) { spoolFullShow("API error " + String(code), 3000); return false; }
    String msg = "Tara new: " + String(newT) + " g";
    if (spoolFullLastOldTara >= 0) msg += "\nold: " + String(spoolFullLastOldTara) + " g";
    spoolFullShow(msg, 4000);
    spoolFullRestWeight = (uint16_t)lroundf(initial);
    lastRemainingShown = spoolFullRestWeight;
    spoolFullRestAt = millis() + 4000;
    spoolFullRestPending = true;
    return true;
}

// Spoolman kennt Orte nur als Text: Name vom Tag, sonst die FilaMan-location_id als Text
static bool spoolmanLocation(int spoolId, const String& uid, int locationId, const String& locationName) {
    int id = spoolmanResolve(spoolId, uid);
    String name = locationName.length() > 0 ? locationName : (locationId > 0 ? String(locationId) : String());
    if (id <= 0 || name.isEmpty()) return false;
    JsonDocument body;
    body["location"] = name;
    String payload;
    serializeJson(body, payload);
    return spoolmanHttp("PATCH", "/api/v1/spool/" + String(id), payload) == 200;
}

// Nach erfolgreichem Tag-Schreiben die UID an der Spule hinterlegen
volatile int spoolmanLastRfidSpool = 0;
volatile int spoolmanLastRfidHttp = 0;
String spoolmanLastRfidUid;
static bool spoolmanRfidResult(const String& uid, int spoolId, bool success) {
    spoolmanLastRfidSpool = spoolId;
    spoolmanLastRfidUid = uid;
    spoolmanLastRfidHttp = success ? -10 : -11;   // -11 = Schreiben fehlgeschlagen
    Serial.printf("spoolmanRfidResult: success=%d spool=%d uid=%s\n", success, spoolId, uid.c_str());
    if (!success || spoolId <= 0 || uid.isEmpty()) return false;
    spoolmanEnsureNfcField();
    // card_uids im U1-Format ergaenzen (Grossbuchstaben, ohne Trenner), vorhandene UIDs behalten
    String card = uid;
    card.replace(":", "");
    card.toUpperCase();
    JsonDocument sp;
    String cards;
    if (spoolmanHttp("GET", "/api/v1/spool/" + String(spoolId), "", &sp) == 200) {
        JsonDocument old;
        if (!deserializeJson(old, sp["extra"]["card_uids"] | "\"\"")) cards = old.as<String>();
    }
    if (("," + cards + ",").indexOf("," + card + ",") < 0) cards = cards.isEmpty() ? card : cards + "," + card;
    // ponytail: UID wird nicht bei anderen Spulen entfernt - bei wiederverwendeten Tags in Spoolman aufraeumen
    JsonDocument body;
    body["extra"]["nfc_id"] = "\"" + uid + "\"";
    body["extra"]["card_uids"] = "\"" + cards + "\"";
    String payload;
    serializeJson(body, payload);
    spoolmanLastRfidHttp = spoolmanHttp("PATCH", "/api/v1/spool/" + String(spoolId), payload);
    return spoolmanLastRfidHttp == 200;
}

// ---- Proxy fuer die Weboberflaeche ----
// ponytail: ein Ergebnis-Slot, neuer Start verwirft das alte. Reicht, die Seite fragt nacheinander.
static volatile uint32_t proxySeq = 0;
static uint32_t proxyResultSeq = 0;
static int proxyResultCode = 0;
static String proxyResultBody;

static void proxyStore(uint32_t seq, int code, String& body) {
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (seq == proxySeq) {
            proxyResultSeq = seq;
            proxyResultCode = code;
            proxyResultBody = std::move(body);
        }
        xSemaphoreGive(queueMutex);
    }
}

// Liste laden und per Filter auf die benoetigten Felder kuerzen, sonst sprengen grosse Inventare den Heap.
// ponytail: komplette Liste im RAM (~150 Byte/Spule) - ok bis einige hundert Spulen, sonst paginieren.
static int spoolmanFetchFiltered(const String& path, JsonDocument& filter, String& out) {
    if (WiFi.status() != WL_CONNECTED) return -1;
    HTTPClient http;
    http.setTimeout(15000);
    http.setReuse(false);
    http.begin(spoolmanUrl + path);
    int code = http.GET();
    if (code == 200) {
        JsonDocument doc;
        if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) code = -2;
        else serializeJson(doc, out);
    }
    http.end();
    Serial.printf("Spoolman proxy GET %s -> %d (%u bytes)\n", path.c_str(), code, out.length());
    return code;
}

static void spoolmanProxy(const ApiRequest& req) {
    String out;
    int code = 400;
    const String& what = req.str1;
    if (what == "spools") {
        JsonDocument f;
        f[0]["id"] = true;
        f[0]["remaining_weight"] = true;
        f[0]["location"] = true;
        f[0]["extra"]["nfc_id"] = true;
        f[0]["filament"]["name"] = true;
        f[0]["filament"]["material"] = true;
        f[0]["filament"]["color_hex"] = true;
        f[0]["filament"]["vendor"]["name"] = true;
        f[0]["filament"]["settings_extruder_temp"] = true;
        f[0]["filament"]["settings_bed_temp"] = true;
        f[0]["filament"]["diameter"] = true;
        f[0]["initial_weight"] = true;
        code = spoolmanFetchFiltered("/api/v1/spool", f, out);
    } else if (what == "filaments") {
        JsonDocument f;
        f[0]["id"] = true;
        f[0]["name"] = true;
        f[0]["material"] = true;
        f[0]["color_hex"] = true;
        f[0]["weight"] = true;
        f[0]["spool_weight"] = true;
        f[0]["vendor"]["name"] = true;
        code = spoolmanFetchFiltered("/api/v1/filament", f, out);
    } else if (what == "locations") {
        JsonDocument resp;
        code = spoolmanHttp("GET", "/api/v1/location", "", &resp);
        if (code == 200) serializeJson(resp, out);
    } else if (what == "create" && req.id1 > 0) {
        JsonDocument body;
        body["filament_id"] = req.id1;
        if (req.str2.length() > 0) body["location"] = req.str2;
        String payload;
        serializeJson(body, payload);
        JsonDocument resp;
        code = spoolmanHttp("POST", "/api/v1/spool", payload, &resp);
        int id = resp["id"] | 0;
        if (code == 200 && id > 0) {
            // Aktuelles Gewicht auf der Waage als erste Messung (Brutto, Spoolman zieht das Leergewicht ab)
            if (req.val > 0) spoolmanHttp("PUT", "/api/v1/spool/" + String(id) + "/measure", "{\"weight\":" + String(req.val, 1) + "}");
            out = "{\"id\":" + String(id) + "}";
        }
    }
    if (code <= 0) code = 502;
    if (out.isEmpty()) out = "{\"error\":\"Spoolman request failed (" + String(code) + ")\"}";
    proxyStore((uint32_t)req.id2, code, out);
}

bool spoolmanProxyStart(const String& what, int filamentId, const String& location, float weight) {
    if (spoolmanUrl.isEmpty()) return false;
    ApiRequest r = makeRequest(API_REQUEST_SPOOLMAN_PROXY);
    r.str1 = what;
    r.id1 = filamentId;
    r.str2 = location;
    r.val = weight;
    r.id2 = (int)++proxySeq;
    return enqueueRequest(r);
}

int spoolmanProxyResult(String& out) {
    int code = 0;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (proxyResultSeq == proxySeq && proxyResultCode != 0) {
            code = proxyResultCode;
            out = std::move(proxyResultBody);
            proxyResultCode = 0;
            proxyResultBody = String();
        }
        xSemaphoreGive(queueMutex);
    }
    return code;
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
                case API_REQUEST_SPOOL_FULL: sendSpoolFull(req.id1, req.str1, req.val, req.bool1); break;
                case API_REQUEST_LOCATE: sendLocation(req.id1, req.str1, req.id2, req.str2, req.str3); break;
                case API_REQUEST_RFID_RESULT: sendRfidResult(req.str1, req.id1, req.id2, req.bool1, req.str3, req.remainingWeight); break;
                case API_REQUEST_TAG_DATA: sendTagData(req.str1); break;
                case API_REQUEST_SPOOLMAN_PROXY: spoolmanProxy(req); break;
                default: break;
            }
            filamanApiState = API_IDLE;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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

void sendSpoolFullAsync(int spoolId, String tagUuid, float weight, bool confirmOverwrite) {
    if (!checkFilamanRegistration()) return;
    ApiRequest r = makeRequest(API_REQUEST_SPOOL_FULL);
    r.id1 = spoolId;
    r.str1 = tagUuid;
    r.val = weight;
    r.bool1 = confirmOverwrite;
    enqueueRequest(r);
}

bool spoolFullConfirmOverwriteAsync() {
    if (!spoolFullOverwritePending) return false;
    spoolFullOverwritePending = false;
    sendSpoolFullAsync(sfSpoolId, sfTagUuid, sfWeight, true);
    return true;
}

void sendLocationAsync(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid, String locationName) {
    if (!checkFilamanRegistration()) return;
    ApiRequest r = makeRequest(API_REQUEST_LOCATE);
    r.id1 = spoolId;
    r.id2 = locationId;
    r.str1 = spoolTagUuid;
    r.str2 = locationTagUuid;
    r.str3 = locationName;
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
