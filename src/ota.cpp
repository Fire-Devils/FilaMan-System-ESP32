#include <Arduino.h>
#include <website.h>
#include <commonFS.h>
#include "scale.h"
#include "nfc.h"
#include "lang.h"


// Globale Variable für den Update-Typ
static int currentUpdateCommand = 0;

// Globale Update-Variablen
static size_t updateTotalSize = 0;
static size_t updateWritten = 0;
static bool isSpiffsUpdate = false;
static size_t lastYieldAt = 0;  // Für WiFi-Yield während großer Uploads

// Upload-Zustand (wird bei index == 0 zurückgesetzt)
static bool uploadFailed = false;
static bool uploadFinished = false;
static const char* uploadErrorMessage = "Update failed";
static bool restartScheduled = false;

// Gibt die beim Upload pausierten Tasks wieder frei.
static void releaseUpdateTasks() {
    nfcReadingTaskSuspendRequest = false;
    scalePauseRequest = false;
}

// Bricht den laufenden Upload ab und gibt die Tasks wieder frei.
static void failUpload(const char* message) {
    uploadFailed = true;
    uploadErrorMessage = message;
    Update.abort();
    releaseUpdateTasks();
}

/**
 * Compares two version strings and determines if version1 is less than version2
 * 
 * @param version1 First version string (format: x.y.z)
 * @param version2 Second version string (format: x.y.z)
 * @return true if version1 is less than version2
 */
bool isVersionLessThan(const String& version1, const String& version2) {
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;
    
    // Parse version1
    sscanf(version1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
    
    // Parse version2
    sscanf(version2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);
    
    // Compare major version
    if (major1 < major2) return true;
    if (major1 > major2) return false;
    
    // Major versions equal, compare minor
    if (minor1 < minor2) return true;
    if (minor1 > minor2) return false;
    
    // Minor versions equal, compare patch
    return patch1 < patch2;
}

void espRestart() {
    yield();
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP.restart();
}


// Einmaliger Task: Neustart verzögert ausführen, ohne den Async-Handler zu blockieren.
static void restartAfterUpdateTask(void* pv) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    ws.textAll("{\"type\":\"updateProgress\",\"progress\":100,\"status\":\"success\",\"message\":\"Update successful! Restarting device...\"}");
    espRestart();
    vTaskDelete(NULL);
}

void sendUpdateProgress(int progress, const char* status = nullptr, const char* message = nullptr) {
    static int lastSentProgress = -1;
    static unsigned long lastSendTime = 0;
    
    unsigned long now = millis();
    
    // Verhindere zu häufige Updates - mindestens 500ms zwischen Progress-Updates
    // Dies reduziert WebSocket-Traffic während des Uploads erheblich
    if (progress == lastSentProgress && !status && !message) {
        return;
    }
    if (now - lastSendTime < 500 && progress < 100 && !status) {
        return;  // Throttle normale Progress-Updates
    }
    
    String progressMsg = "{\"type\":\"updateProgress\",\"progress\":" + String(progress);
    if (status) {
        progressMsg += ",\"status\":\"" + String(status) + "\"";
    }
    if (message) {
        progressMsg += ",\"message\":\"" + String(message) + "\"";
    }
    progressMsg += "}";
    
    // Sende Progress-Update (nur einmal, nicht mehrfach)
    ws.textAll(progressMsg);
    
    // Yield für WiFi-Stack nach jedem WebSocket-Send
    vTaskDelay(pdMS_TO_TICKS(10));
    
    lastSendTime = now;
    lastSentProgress = progress;
}

void handleUpdate(AsyncWebServer &server) {
    AsyncCallbackWebHandler* updateHandler = new AsyncCallbackWebHandler();
    updateHandler->setUri("/update");
    updateHandler->setMethod(HTTP_POST);
    
    // Check if current version is less than defined TOOLVERSION before proceeding with update
    if (isVersionLessThan(VERSION, TOOLDVERSION)) {
        updateHandler->onRequest([](AsyncWebServerRequest *request) {
            request->send(400, "application/json", 
                "{\"success\":false,\"message\":\"Your current version is too old. Please perform a full upgrade.\"}");
        });
        server.addHandler(updateHandler);
        return;
    }

    updateHandler->onUpload([](AsyncWebServerRequest *request, String filename,
                             size_t index, uint8_t *data, size_t len, bool final) {

        if (!index) {
            uploadFailed = false;
            uploadFinished = false;
            uploadErrorMessage = "Update failed";
            updateTotalSize = request->contentLength();
            updateWritten = 0;
            lastYieldAt = 0;  // Reset yield counter
            isSpiffsUpdate = (filename.indexOf("website") > -1);

            // Bricht der Client ab, bevor das Update fertig ist: aufräumen.
            request->onDisconnect([]() {
                if (!uploadFinished) {
                    Update.abort();
                    uploadFailed = true;
                    releaseUpdateTasks();
                }
            });

            bool started;
            if (isSpiffsUpdate) {
                const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
                started = partition && Update.begin(partition->size, U_SPIFFS);
            } else {
                started = Update.begin(updateTotalSize);
            }
            if (!started) {
                failUpload("Update initialization failed");
                return;
            }

            // Erst nach erfolgreichem begin() die anderen Tasks anhalten.
            nfcReadingTaskSuspendRequest = true;  // Reader pausiert selbst (hält ggf. den I2C-Lock)
            scalePauseRequest = true;             // Scale-Loop pausiert selbst (hält ggf. den Display-Mutex)

            if (isSpiffsUpdate) {
                sendUpdateProgress(5, "starting", "Starting SPIFFS update...");
            } else {
                sendUpdateProgress(0, "starting", "Starting firmware update...");
            }
        }

        if (uploadFailed) return;  // weitere Chunks ignorieren

        if (len) {
            if (Update.write(data, len) != len) {
                failUpload("Write failed");
                return;
            }

            updateWritten += len;

            // KRITISCH: Yield alle 32KB für WiFi-Stack bei großen Uploads (1.5-2MB)
            // Ohne dies kann der WiFi-Stack blockieren und der Upload hängen bleiben
            if (updateWritten - lastYieldAt >= 32768) {
                vTaskDelay(pdMS_TO_TICKS(5));  // Kurze Pause für WiFi-Stack
                lastYieldAt = updateWritten;
            }

            int currentProgress = 0;
            if (updateTotalSize > 0) {
                if (isSpiffsUpdate) {
                    // SPIFFS: 5-100% für Upload
                    currentProgress = 5 + (updateWritten * 95) / updateTotalSize;
                } else {
                    // Firmware: 0-100% für Upload
                    currentProgress = (updateWritten * 100) / updateTotalSize;
                }
            }

            static int lastProgress = -1;
            // Nur alle 5% Progress-Updates senden (reduziert WebSocket-Traffic)
            if (currentProgress != lastProgress && (currentProgress % 5 == 0 || final)) {
                sendUpdateProgress(currentProgress, "uploading");
                oledShowProgressBar(currentProgress, 100, tr(STR_UPDATE), tr(STR_DOWNLOAD));
                lastProgress = currentProgress;
            }
        }

        if (final) {
            if (Update.end(true)) {
                uploadFinished = true;
            } else {
                failUpload("Update finalization failed");
            }
        }
    });

    updateHandler->onRequest([](AsyncWebServerRequest *request) {
        if (!(uploadFinished && !uploadFailed && !Update.hasError())) {
            String msg = String("{\"success\":false,\"message\":\"") + uploadErrorMessage + "\"}";
            request->send(400, "application/json", msg);
            return;
        }

        // Erste 100% Nachricht
        ws.textAll("{\"type\":\"updateProgress\",\"progress\":100,\"status\":\"success\",\"message\":\"Update successful! Restarting device...\"}");

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
            "{\"success\":true,\"message\":\"Update successful! Restarting device...\"}");
        response->addHeader("Connection", "close");
        request->send(response);

        // Zweite Nachricht + Neustart verzögert in eigenem Task (nicht im Async-Handler blockieren)
        if (!restartScheduled) {
            restartScheduled = true;
            if (xTaskCreate(restartAfterUpdateTask, "OtaRestart", 3072, NULL, 1, NULL) != pdPASS) {
                ESP.restart();
            }
        }
    });

    server.addHandler(updateHandler);
}