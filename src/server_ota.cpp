// Firmware-Updates per WLAN vom FilaMan-Server (siehe README "Updates per WLAN").
#include "server_ota.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "api.h"
#include "nfc.h"
#include "scale.h"
#include "display.h"
#include "lang.h"

#ifndef VERSION
  #define VERSION "0.0.0"
#endif

// Marker, den der Server aus dem Image liest (Regex siehe Server-Doku).
extern "C" __attribute__((used)) const char FILAMAN_FW_INFO[] =
    "FMFWINFO1:project=" FW_PROJECT ";version=" VERSION ";";

// Praefix, das im neuen Image vorkommen muss (verhindert Flashen des falschen Projekts).
static const char PROJECT_NEEDLE[] = "FMFWINFO1:project=" FW_PROJECT ";";
static const size_t NEEDLE_LEN = sizeof(PROJECT_NEEDLE) - 1;

#define SRVOTA_NS               "srvota"
// Erst nach 3 Minuten: kurz nach dem WLAN-Connect fallen in manchen Netzen (IoT-AP, Routing)
// Verbindungen fuer bis zu 1,5 Minuten aus.
#define SRVOTA_FIRST_CHECK_MS   180000UL
#define SRVOTA_FIRST_JITTER_MS  30000UL
#define SRVOTA_INTERVAL_MS      (6UL * 3600UL * 1000UL)
#define SRVOTA_JITTER_MS        (30UL * 60UL * 1000UL)
#define SRVOTA_BUSY_RETRY_MS    60000UL
#define SRVOTA_HINT_MIN_INTERVAL_MS 1800000UL  // gleiche Hinweis-Version hoechstens alle 30 min erneut pruefen
#define SRVOTA_FAIL_RETRY_MS    60000UL    // schneller neuer Versuch nach fehlgeschlagenem Download
#define SRVOTA_FAIL_RETRIES     3          // so viele schnelle Wiederholungen hintereinander, dann normales Intervall
#define SRVOTA_CONFIRM_MS       300000UL   // Frist bis zur Server-Bestaetigung nach einem Update
#define SRVOTA_MAX_BOOTS        3
#define SRVOTA_CHUNK            4096
#define SRVOTA_MAX_ATTEMPTS     10         // Download-Versuche inkl. Fortsetzungen per Range
#define SRVOTA_STALL_MS         10000UL    // so lange ohne Daten, dann neu verbinden (Fortsetzen ist billiger als TCP-Backoff)
#define SRVOTA_MAX_DOWNLOAD_MS  (15UL * 60UL * 1000UL)

static volatile bool otaPending = false;   // neue Firmware laeuft, noch nicht bestaetigt
static volatile bool otaRunning = false;   // Download/Flash laeuft
static volatile bool reportOkPending = false;  // nach Bestaetigung "ok" aus der neuen Firmware melden
static String pendingFrom, pendingTo;

bool serverOtaInProgress() { return otaRunning; }
bool firmwareUpdateActive() { return otaRunning || otaManualUploadActive(); }

// ---------------------------------------------------------------- NVS ----

static void clearPendingNvs() {
    Preferences p;
    if (!p.begin(SRVOTA_NS, false)) return;
    p.putBool("pending", false);
    p.putUChar("boots", 0);
    p.end();
}

void serverOtaConfirm() {
    if (!otaPending) return;
    otaPending = false;
    clearPendingNvs();
    // Der "ok"-Bericht vor dem Neustart kann verloren gehen; die laufende neue Firmware meldet es sicher.
    if (pendingTo.length() > 0) reportOkPending = true;
    Serial.println("ServerOTA: new firmware confirmed");
}

void serverOtaResetState() {
    otaPending = false;
    Preferences p;
    if (!p.begin(SRVOTA_NS, false)) return;
    p.putBool("pending", false);
    p.putUChar("boots", 0);
    p.putBool("rb", false);
    p.remove("bad");
    p.end();
}

// Schaltet auf die andere App-Partition zurueck und startet neu. Kehrt nur bei Fehler zurueck.
static void rollbackAndRestart(const char* reason) {
    Serial.printf("ServerOTA: rollback (%s)\n", reason);
    const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    if (other && esp_ota_get_partition_description(other, &desc) == ESP_OK &&
        esp_ota_set_boot_partition(other) == ESP_OK) {
        Preferences p;
        if (p.begin(SRVOTA_NS, false)) {
            String to = p.getString("to", "");
            p.putBool("pending", false);
            p.putUChar("boots", 0);
            p.putBool("rb", true);
            p.putString("rberr", reason);
            if (to.length() > 0) p.putString("bad", to);  // diese Version nicht erneut automatisch installieren
            p.end();
        }
        delay(200);
        ESP.restart();
    }
    // Keine gueltige Rueckfall-Firmware: nicht in Dauerschleife bleiben.
    Serial.println("ServerOTA: no valid fallback image, keeping current firmware");
    otaPending = false;
    clearPendingNvs();
}

void serverOtaEarlyInit() {
    Serial.println(FILAMAN_FW_INFO);  // haelt den Marker im Image (gc-sections)
    Preferences p;
    if (!p.begin(SRVOTA_NS, false)) return;
    bool pending = p.getBool("pending", false);
    if (pending) {
        pendingFrom = p.getString("from", "");
        pendingTo = p.getString("to", "");
        uint8_t boots = p.getUChar("boots", 0) + 1;
        p.putUChar("boots", boots);
        p.end();
        if (boots > SRVOTA_MAX_BOOTS) {
            rollbackAndRestart("crash loop");
        }
        otaPending = true;
        Serial.printf("ServerOTA: unconfirmed firmware, boot %u\n", boots);
        return;
    }
    p.end();
}

// ------------------------------------------------------------- HTTP ----

static bool otaBusy() {
    return otaManualUploadActive() || nfcWriteInProgress || scanRequestActive ||
           scaleCalibrationActive || nfcReaderState == NFC_WRITING ||
           (nfcReaderState == NFC_READ_SUCCESS && !tagProcessed) ||
           weightCounterToApi > 0 || filamanApiState != API_IDLE;
}

static void prepareHttp(HTTPClient& http, const String& url, uint16_t timeoutMs) {
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    // Redirects NICHT folgen: der Device-Token darf nie an fremde Hosts gehen.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.begin(url);
    http.addHeader("Authorization", "Device " + filamanToken);
}

// result: started|ok|failed|rolled_back. Liefert true bei 2xx (bestaetigt dann auch eine neue Firmware).
static bool sendOtaReport(const String& from, const String& to, const char* result, const String& error) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    prepareHttp(http, filamanUrl + "/api/v1/devices/ota-report", 10000);
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["from_version"] = from;
    doc["to_version"] = to;
    doc["result"] = result;
    if (error.length() > 0) doc["error"] = error; else doc["error"] = nullptr;
    String payload;
    serializeJson(doc, payload);
    int code = http.POST(payload);
    http.end();
    Serial.printf("ServerOTA: report %s -> HTTP %d\n", result, code);
    if (code >= 200 && code < 300) {
        serverOtaConfirm();
        return true;
    }
    return false;
}

static bool urlIsSafe(const String& u) {
    return u.startsWith("/api/v1/devices/firmware/") && u.indexOf("..") < 0 &&
           u.indexOf("://") < 0 && u.length() < 200;
}

static bool versionIsSane(const String& v) {
    if (v.length() == 0 || v.length() > 32) return false;
    for (size_t i = 0; i < v.length(); i++) {
        char c = v[i];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '+' || c == '-')) return false;
    }
    return true;
}

// Naive Suche; genuegt fuer 38 Byte Nadel, Blockgrenzen regelt der Aufrufer per Carry-Over.
static bool containsNeedle(const uint8_t* buf, size_t len) {
    if (len < NEEDLE_LEN) return false;
    for (size_t i = 0; i + NEEDLE_LEN <= len; i++) {
        if (buf[i] == (uint8_t)PROJECT_NEEDLE[0] && memcmp(buf + i, PROJECT_NEEDLE, NEEDLE_LEN) == 0) return true;
    }
    return false;
}

// Durchsucht das geschriebene Image in der Zielpartition nach dem Projekt-Marker.
static bool partitionHasProject(const esp_partition_t* part, size_t size) {
    const size_t carryMax = NEEDLE_LEN - 1;
    uint8_t* work = (uint8_t*)malloc(carryMax + SRVOTA_CHUNK);
    if (!work) return false;
    size_t carry = 0, off = 0;
    bool found = false;
    while (off < size && !found) {
        size_t n = min((size_t)SRVOTA_CHUNK, size - off);
        if (esp_partition_read(part, off, work + carry, n) != ESP_OK) break;
        found = containsNeedle(work, carry + n);
        size_t total = carry + n;
        carry = min(carryMax, total);
        memmove(work, work + total - carry, carry);
        off += n;
        vTaskDelay(1);
    }
    free(work);
    return found;
}

static void showProgress(int pct) {
    oledResetActivityTimer();  // Display nicht in den Schlaf gehen lassen
    oledShowProgressBar(pct, 100, tr(STR_UPDATE), tr(STR_DOWNLOAD));
    // Lange halten: zwischen zwei Fortschrittsschritten darf nichts anderes zeichnen.
    oledSetPriority(DISPLAY_PRIORITY_ACTION, 120000);
}

static void releaseTasks() {
    nfcReadingTaskSuspendRequest = false;
    scalePauseRequest = false;
    oledClearPriority();
}

// Laedt und installiert die Firmware. Kehrt nur bei Fehler zurueck (bei Erfolg Neustart).
// Rueckgabe true = begonnener Installationsversuch ist fehlgeschlagen (-> schneller neuer Versuch).
static bool installFirmware(const String& newVersion, uint32_t size, const String& sha256, const String& path) {
    String from = VERSION;
    String err;
    esp_ota_handle_t otaHandle = 0;
    bool otaBegun = false, otaEnded = false;
    auto fail = [&](const String& msg) {
        Serial.printf("ServerOTA: FAILED: %s\n", msg.c_str());
        if (otaBegun && !otaEnded) esp_ota_abort(otaHandle);  // nach esp_ota_end ist das Handle ungueltig
        releaseTasks();
        otaRunning = false;
        sendOtaReport(from, newVersion, "failed", msg);
    };

    const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
    if (!target) { fail("no OTA partition"); return false; }
    if (size == 0 || size > target->size) { fail("image size invalid for partition"); return false; }

    otaRunning = true;
    sendOtaReport(from, newVersion, "started", "");

    // NFC/Scale kooperativ pausieren (wie beim manuellen Upload in ota.cpp).
    nfcReadingTaskSuspendRequest = true;
    scalePauseRequest = true;
    for (int i = 0; i < 20 && !nfcReadingTaskSuspendState; i++) vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(300));
    showProgress(0);

    // esp_ota_begin loescht den ganzen Bereich vorab (einige Sekunden), damit waehrend des
    // Downloads kein Flash-Erase den WLAN-Empfang blockiert. Muss vor der HTTP-Verbindung passieren.
    const uint32_t eraseStart = millis();
    esp_err_t oerr = esp_ota_begin(target, size, &otaHandle);
    if (oerr != ESP_OK) { fail(String("esp_ota_begin: ") + esp_err_to_name(oerr)); return true; }
    otaBegun = true;
    Serial.printf("ServerOTA: erase %u bytes took %lu ms\n", (unsigned)size, (unsigned long)(millis() - eraseStart));

    const size_t carryMax = NEEDLE_LEN - 1;
    uint8_t* work = (uint8_t*)malloc(carryMax + SRVOTA_CHUNK);
    if (!work) { fail("out of memory"); return true; }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    size_t written = 0, carry = 0, sinceYield = 0;
    int lastPct = -1, lastLogPct = -10;
    bool markerSeen = false;
    bool ok = true;
    const uint32_t downloadStart = millis();
    // Langsame oder abreissende Verbindungen (z. B. ueber einen Router in ein anderes Netz):
    // bei einem Abriss mit HTTP-Range am letzten Byte weitermachen statt neu anzufangen.
    for (int attempt = 0; attempt < SRVOTA_MAX_ATTEMPTS && ok && written < size; attempt++) {
        if (attempt > 0) {
            Serial.printf("ServerOTA: resume at %u/%u (attempt %d)\n", (unsigned)written, (unsigned)size, attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        if (WiFi.status() != WL_CONNECTED) { err = "WiFi lost"; continue; }

        HTTPClient http;
        prepareHttp(http, filamanUrl + path, 15000);
        http.useHTTP10(true);  // kein Chunked-Encoding: Content-Length ist Pflicht
        if (written > 0) http.addHeader("Range", "bytes=" + String((unsigned)written) + "-");
        int code = http.GET();
        int expect = written > 0 ? 206 : HTTP_CODE_OK;
        if (code != expect) {
            http.end();
            err = "download HTTP " + String(code);
            Serial.printf("ServerOTA: GET failed %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
            if (code > 0 && code != 206 && code != 200) ok = false;  // echte Serverantwort: kein Wiederholen
            continue;
        }
        int len = http.getSize();
        if (len < 0 || (uint32_t)len != size - written) { http.end(); err = "content-length mismatch"; ok = false; break; }

        WiFiClient* stream = http.getStreamPtr();
        uint32_t lastData = millis();
        bool stalled = false;
        while (written < size) {
            if (WiFi.status() != WL_CONNECTED) { err = "WiFi lost"; stalled = true; break; }
            if (millis() - downloadStart > SRVOTA_MAX_DOWNLOAD_MS) { err = "download took longer than 15 min"; ok = false; break; }
            // available() liefert int; negative Werte (Fehler) duerfen nicht als riesige size_t durchrutschen.
            int availRaw = stream->available();
            int n = 0;
            if (availRaw > 0) {
                size_t want = min(min((size_t)availRaw, (size_t)SRVOTA_CHUNK), size - written);
                n = stream->read(work + carry, want);
            }
            if (n <= 0) {
                if (!stream->connected()) { err = "connection lost"; stalled = true; break; }
                if (millis() - lastData > SRVOTA_STALL_MS) { err = "download timeout"; stalled = true; break; }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            lastData = millis();
            mbedtls_sha256_update(&sha, work + carry, n);
            if (!markerSeen) markerSeen = containsNeedle(work, carry + n);
            oerr = esp_ota_write(otaHandle, work + carry, n);
            if (oerr != ESP_OK) { err = String("flash write: ") + esp_err_to_name(oerr); ok = false; break; }
            size_t total = carry + n;
            carry = min(carryMax, total);
            memmove(work, work + total - carry, carry);
            written += n;

            int pct = (int)(((uint64_t)written * 100) / size);
            if (pct != lastPct && pct % 2 == 0) { showProgress(pct); lastPct = pct; }
            if (pct >= lastLogPct + 10) {
                Serial.printf("ServerOTA: %d%% (%u bytes, %lus)\n", pct, (unsigned)written, (unsigned long)((millis() - downloadStart) / 1000));
                lastLogPct = pct;
            }
            sinceYield += n;
            if (sinceYield >= 8192) { sinceYield = 0; vTaskDelay(1); }
        }
        http.end();
        if (stalled) {
            Serial.printf("ServerOTA: stall (%s) at %u/%u, %lus, RSSI %d\n", err.c_str(), (unsigned)written, (unsigned)size,
                          (unsigned long)((millis() - downloadStart) / 1000), (int)WiFi.RSSI());
        }
        if (!stalled) break;  // fertig oder harter Fehler
    }
    if (ok && written < size) ok = false;  // alle Versuche aufgebraucht, err steht schon
    free(work);

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (!ok) { fail(err); return true; }

    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
    String got = String(hex);
    String want = sha256; want.toLowerCase();
    if (got != want) { fail("SHA-256 mismatch"); return true; }
    if (!markerSeen) { fail("image is not for this project (" FW_PROJECT ")"); return true; }

    // esp_ota_end prueft das Image; danach ist das Handle ungueltig (auch bei Fehler).
    otaEnded = true;
    oerr = esp_ota_end(otaHandle);
    if (oerr != ESP_OK) { fail(String("esp_ota_end: ") + esp_err_to_name(oerr)); return true; }

    // Image aus der Zielpartition nochmals pruefen (Boot-Partition ist noch nicht gesetzt).
    if (!partitionHasProject(target, size)) { fail("project marker missing in written image"); return true; }

    // pending VOR dem Aktivieren setzen: bricht der Strom danach ab, greift trotzdem der Rollback.
    {
        Preferences p;
        if (p.begin(SRVOTA_NS, false)) {
            p.putBool("pending", true);
            p.putString("from", from);
            p.putString("to", newVersion);
            p.putUChar("boots", 0);
            p.putBool("rb", false);
            p.end();
        }
    }
    oerr = esp_ota_set_boot_partition(target);
    if (oerr != ESP_OK) {
        clearPendingNvs();
        fail(String("esp_ota_set_boot_partition: ") + esp_err_to_name(oerr));
        return true;
    }

    showProgress(100);
    Serial.printf("ServerOTA: %s -> %s installed, restarting\n", from.c_str(), newVersion.c_str());
    sendOtaReport(from, newVersion, "ok", "");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
    return false;
}

// Rueckgabe true = schneller neuer Versuch sinnvoll: Installationsversuch fehlgeschlagen oder
// Server nicht erreichbar (Netzfehler). Nicht bei: up to date, 404, Manifestfehler.
static bool checkForUpdate() {
    String url = filamanUrl + "/api/v1/devices/firmware/latest?project=" FW_PROJECT;
    HTTPClient http;
    prepareHttp(http, url, 10000);
    int code = http.GET();
    if (code == 404) { http.end(); Serial.println("ServerOTA: no firmware available"); return false; }
    if (code < 200 || code >= 300) { http.end(); Serial.printf("ServerOTA: manifest HTTP %d\n", code); return code < 0; }
    serverOtaConfirm();
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) { Serial.println("ServerOTA: manifest parse error"); return false; }
    String project = doc["project"] | "";
    String version = doc["version"] | "";
    String sha = doc["sha256"] | "";
    String path = doc["url"] | "";
    uint32_t size = doc["size"] | 0;

    if (project != FW_PROJECT) { Serial.println("ServerOTA: project mismatch, abort"); return false; }
    if (!versionIsSane(version)) { Serial.println("ServerOTA: bad version string"); return false; }
    if (version == VERSION) { Serial.println("ServerOTA: up to date"); return false; }
    if (sha.length() != 64) { Serial.println("ServerOTA: bad sha256"); return false; }
    if (!urlIsSafe(path)) { Serial.println("ServerOTA: unsafe download url"); return false; }
    if (size == 0) { Serial.println("ServerOTA: bad size"); return false; }

    Preferences p;
    if (p.begin(SRVOTA_NS, true)) {
        String bad = p.getString("bad", "");
        p.end();
        if (bad == version) { Serial.println("ServerOTA: version was rolled back before, skipping"); return false; }
    }
    if (otaPending) { Serial.println("ServerOTA: previous update unconfirmed, skipping"); return false; }

    Serial.printf("ServerOTA: update %s -> %s (%u bytes)\n", VERSION, version.c_str(), (unsigned)size);
    return installFirmware(version, size, sha, path);
}

// Einmaliger Bericht nach einem Rollback (laeuft in der alten Firmware).
static bool reportRollbackIfNeeded() {
    Preferences p;
    if (!p.begin(SRVOTA_NS, true)) return true;
    bool rb = p.getBool("rb", false);
    String from = p.getString("from", "");
    String to = p.getString("to", "");
    // isKey() vermeidet die NOT_FOUND-Fehlermeldung von Preferences im Log
    String err = p.isKey("rberr") ? p.getString("rberr", "rollback") : String("rollback");
    p.end();
    if (!rb) return true;
    // Nach dem Rollback laeuft die alte Firmware: "from" = alte, "to" = fehlgeschlagene Version.
    if (!sendOtaReport(from, to, "rolled_back", err)) return false;
    Preferences w;
    if (w.begin(SRVOTA_NS, false)) { w.putBool("rb", false); w.end(); }
    return true;
}

// Update-Hinweis aus dem Heartbeat (kommt aus dem FilaManApi-Task, wird im ServerOta-Task gelesen).
static portMUX_TYPE hintMux = portMUX_INITIALIZER_UNLOCKED;
static char hintVersion[33] = "";
static bool hintPending = false;

void serverOtaUpdateHint(const char* version) {
    if (!version) return;
    size_t len = strlen(version);
    if (len == 0 || len > 32) return;
    String v(version);
    if (!versionIsSane(v) || v == VERSION) return;
    portENTER_CRITICAL(&hintMux);
    memcpy(hintVersion, version, len + 1);
    hintPending = true;
    portEXIT_CRITICAL(&hintMux);
}

static void serverOtaTask(void*) {
    bool scheduled = false;
    uint32_t nextCheckAt = 0;
    uint32_t nextRbReportAt = 0;
    bool rbDone = false;
    // Frist zaehlt nur Zeit MIT WLAN: faellt Router/WLAN nach dem Update aus oder
    // haengt setup() im WiFi-Portal, soll das nicht zu einem grundlosen Rollback fuehren.
    // Abstuerze faengt weiterhin der Boot-Zaehler ab.
    uint32_t pendingOnlineMs = 0;
    int failRetries = 0;
    char lastHintVersion[33] = "";   // zuletzt per Hinweis geprueft
    uint32_t lastHintCheckAt = 0;
    bool haveHintCheck = false;

    // Gemeinsamer Pfad fuer geplante und per Hinweis ausgeloeste Pruefung.
    auto runCheck = [&]() {
        bool installFailed = checkForUpdate();  // bei Erfolg Neustart; sonst naechster Termin
        if (installFailed && failRetries < SRVOTA_FAIL_RETRIES) {
            failRetries++;
            nextCheckAt = millis() + SRVOTA_FAIL_RETRY_MS;
        } else {
            failRetries = 0;
            nextCheckAt = millis() + SRVOTA_INTERVAL_MS + (esp_random() % SRVOTA_JITTER_MS);
        }
    };

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = millis();
        bool online = checkFilamanRegistration() && WiFi.status() == WL_CONNECTED;

        if (otaPending) {
            if (!checkFilamanRegistration()) {
                serverOtaConfirm();  // Einrichtungsmodus: es gibt keinen Server zu fragen
            } else if (WiFi.status() == WL_CONNECTED) {
                pendingOnlineMs += 1000;
                if (pendingOnlineMs >= SRVOTA_CONFIRM_MS) {
                    rollbackAndRestart("no server contact after update");
                }
            }
        }

        if (!online || otaRunning) continue;

        static uint32_t nextOkReportAt = 0;
        if (reportOkPending && (int32_t)(now - nextOkReportAt) >= 0) {
            if (sendOtaReport(pendingFrom, pendingTo, "ok", "")) reportOkPending = false;
            else nextOkReportAt = now + 30000UL;  // Server gerade nicht erreichbar: spaeter erneut
        }

        if (!rbDone && (int32_t)(now - nextRbReportAt) >= 0) {
            rbDone = reportRollbackIfNeeded();
            nextRbReportAt = now + 60000UL;
        }

        bool checked = false;
        if (!scheduled) {
            nextCheckAt = now + SRVOTA_FIRST_CHECK_MS + (esp_random() % SRVOTA_FIRST_JITTER_MS);
            scheduled = true;
        } else if ((int32_t)(now - nextCheckAt) >= 0) {
            if (otaBusy()) {
                nextCheckAt = now + SRVOTA_BUSY_RETRY_MS;
            } else {
                runCheck();
                checked = true;
            }
        }

        // Update-Hinweis vom Server: sofort pruefen (auch vor Ablauf der Startfrist), gedrosselt.
        if (!checked && !otaBusy()) {
            char hv[33];
            bool have = false;
            portENTER_CRITICAL(&hintMux);
            if (hintPending) {
                memcpy(hv, hintVersion, sizeof(hv));
                hintPending = false;
                have = true;
            }
            portEXIT_CRITICAL(&hintMux);
            if (have) {
                uint32_t t = millis();
                bool newVersion = !haveHintCheck || strcmp(hv, lastHintVersion) != 0;
                if (newVersion || (uint32_t)(t - lastHintCheckAt) >= SRVOTA_HINT_MIN_INTERVAL_MS) {
                    memcpy(lastHintVersion, hv, sizeof(lastHintVersion));
                    lastHintCheckAt = t;
                    haveHintCheck = true;
                    Serial.printf("ServerOTA: update hint %s, checking now\n", hv);
                    runCheck();
                }
            }
        }
    }
}

void serverOtaStart() {
    xTaskCreatePinnedToCore(serverOtaTask, "ServerOta", 12288, NULL, 1, NULL, 1);
}
