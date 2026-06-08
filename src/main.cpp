#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Board-specific pin definitions
#include "board_config.h"

#include "wlan.h"
#include "config.h"
#include "website.h"
#include "api.h"
#include "display.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include "commonFS.h"
#include "lang.h"

bool mainTaskWasPaused = 0;
uint8_t scaleTareCounter = 0;
bool touchSensorConnected = false;
bool booting = true;

// ##### SETUP #####
void setup() {
  Serial.begin(115200);

  uint64_t chipid;

  chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
  Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32)); //print High 2 bytes
  Serial.printf("%08X\n", (uint32_t)chipid); //print Low 4bytes.

  // Initialize SPIFFS
  initializeFileSystem();

  // Load language setting from NVS (before display init)
  loadLanguage();

  // Init I2C with defined pins and pull-up.
  // Request the ESP32 internal pull-up resistors on
  pinMode(I2C_SDA, INPUT_PULLUP); 
  pinMode(I2C_SCL, INPUT_PULLUP);
  delay(5);

    Serial.printf("Initializing Shared I2C Bus... ");
  // Check if pins are defined (non-zero)
  if (I2C_SDA && I2C_SCL) {
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.printf("Done. (SDA: %d, SCL: %d)\n", I2C_SDA, I2C_SCL);
  } else {
    // Fallback to framework defaults (21/22) if undefined
    Wire.begin();
    Serial.println(F("Done. (Default Pins)"));
  }

  // Start Display
  setupDisplay();

  // Init Network (WiFi/Ethernet)
  initNetwork();

  // Webserver
  setupWebserver(server);

  // FilaMan API
  initFilaman();

  // NFC Reader
  startNfc();

  // Touch Sensor
  pinMode(TTP223_PIN, INPUT_PULLUP);
  if (digitalRead(TTP223_PIN) == LOW)
  {
    Serial.println("Touch Sensor is connected");
    touchSensorConnected = true;
  }

  // Scale
  start_scale(touchSensorConnected);
  if (scaleConnected){
    scaleTareRequest = true;
  }

  // WDT initialisieren mit 10 Sekunden Timeout
  bool panic = true; // Wenn true, löst ein WDT-Timeout einen System-Panik aus
  esp_task_wdt_init(10, panic);

  booting = false;
  oledShowProgressBar(6, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_INIT_DONE));

  // Aktuellen Task (loopTask) zum Watchdog hinzufügen
  esp_task_wdt_add(NULL);

  if ( !scaleConnected ) {
    // Clear Display after Boot
    oledDisplayText(tr(STR_NOSCALE_PROMPT));
  }
}


/**
 * Safe interval check that handles millis() overflow
 * @param currentTime Current millis() value
 * @param lastTime Last recorded time
 * @param interval Desired interval in milliseconds
 * @return True if interval has elapsed
 */
bool intervalElapsed(unsigned long currentTime, unsigned long &lastTime, unsigned long interval) {
  if (currentTime - lastTime >= interval || currentTime < lastTime) {
    lastTime = currentTime;
    return true;
  }
  return false;
}

unsigned long lastWeightReadTime = 0;
const unsigned long weightReadInterval = 1000; // 1 second

unsigned long lastFilamanHeartbeatTime = 0;
unsigned long lastWifiCheckTime = 0;
unsigned long lastTopRowUpdateTime = 0;

uint8_t weightSend = 0;
int16_t lastWeight = 0;

// Button debounce variables
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 500; // 500 ms debounce delay

unsigned long lastConnErrorShowTime = 0;
const unsigned long connErrorShowInterval = 10000; // Show connection error every 10 seconds if exists

// ##### PROGRAM START #####
void loop() {
  unsigned long currentMillis = millis();

  // Handle connection errors (not registered or not connected)
  if (intervalElapsed(currentMillis, lastConnErrorShowTime, connErrorShowInterval)) {
      if (!filamanRegistered && oledCanUpdate(DISPLAY_PRIORITY_WARNING)) {
          oledShowConnectionError(tr(STR_NOT_REGISTERED), WiFi.localIP().toString());
          oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
          mainTaskWasPaused = true;
      } else if (!filamanConnected && oledCanUpdate(DISPLAY_PRIORITY_WARNING)) {
          oledShowConnectionError(tr(STR_API_CONN_LOST), WiFi.localIP().toString());
          oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
          mainTaskWasPaused = true;
      } else if ( !scaleConnected ){
          // everything fine again: without scale manual clearing of the error msg is needed
          oledDisplayText(tr(STR_NOSCALE_PROMPT));
      }
  }

  // Überprüfe den Status des Touch Sensors (nur wenn Waage vorhanden)
  if (scaleConnected && touchSensorConnected && digitalRead(TTP223_PIN) == HIGH && currentMillis - lastButtonPress > debounceDelay)
  {
    lastButtonPress = currentMillis;
    scaleTareRequest = true;
    oledResetActivityTimer(); // Wake display on touch press
  }

  // Überprüfe regelmäßig die WLAN-Verbindung
  if (intervalElapsed(currentMillis, lastWifiCheckTime, WIFI_CHECK_INTERVAL))
  {
    checkWiFiConnection();
  }

  // Periodic display update
  if (intervalElapsed(currentMillis, lastTopRowUpdateTime, DISPLAY_UPDATE_INTERVAL))
  {
    oledShowTopRow();
    oledCheckSleep(); // Put display to sleep if timeout elapsed
  }

  // WebSocket Cleanup alle 5 Sekunden (häufiger als vorher)
  // Tote Clients können WiFi-Ressourcen blockieren
  static unsigned long lastWsCleanup = 0;
  if (currentMillis - lastWsCleanup >= 5000) {
    ws.cleanupClients();
    lastWsCleanup = currentMillis;
  }

  // Periodic FilaMan heartbeat
  if (intervalElapsed(currentMillis, lastFilamanHeartbeatTime, FILAMAN_HEARTBEAT_INTERVAL))
  {
    sendHeartbeatAsync();
  }

  // If scale is not calibrated, only show a warning
  if (scaleConnected && !scaleCalibrated)
  {
    // Do not show the warning if the calibratin process is onging
    if(!scaleCalibrationActive){
      oledDisplayText(tr(STR_SCALE_NOT_CALIBRATED));
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  if (scaleConnected && scaleCalibrated)
  {
    // Ausgabe der Waage auf Display
    // Block weight display during NFC write operations and higher-priority display messages
    if(pauseMainTask == 0 && !nfcWriteInProgress && oledCanUpdate(DISPLAY_PRIORITY_STATUS))
    {
      // Use filtered weight for smooth display, but still check API weight for significant changes
      int16_t displayWeight = getFilteredDisplayWeight();
      if (mainTaskWasPaused || (weight != lastWeight && (nfcReaderState == NFC_IDLE || tagProcessed)))
      {
        oledShowWeight((abs(displayWeight) < 2) ? 0 : displayWeight);
        oledSetPriority(DISPLAY_PRIORITY_STATUS, 0);  // Weight can always be overwritten
      }
      mainTaskWasPaused = false;
    }
    else
    {
      mainTaskWasPaused = true;
    }


    // Wenn Timer abgelaufen und nicht gerade ein RFID-Tag geschrieben wird
    if (currentMillis - lastWeightReadTime >= weightReadInterval && nfcReaderState < NFC_WRITING)
    {
      lastWeightReadTime = currentMillis;

      // Prüfen ob das Gewicht gleich bleibt und dann senden
      if (abs(weight - lastWeight) <= 2 && weight > 5)
      {
        weightCounterToApi++;
        // Show stable weight feedback when approaching send threshold
        if (weightCounterToApi == 3 && nfcReaderState == NFC_READ_SUCCESS && !tagProcessed && weightSend == 0) {
          oledShowProgressBar(2, 4, tr(STR_SPOOL_TAG), tr(STR_WEIGHT_STABLE));
          oledSetPriority(DISPLAY_PRIORITY_INFO, 1000);
        }
      }
      else
      {
        // Show visual feedback when tag is present and weight is unstable
        if (weightCounterToApi > 0 && nfcReaderState == NFC_READ_SUCCESS && !tagProcessed && weightSend == 0) {
          oledShowProgressBar(1, 4, tr(STR_SPOOL_TAG), tr(STR_WEIGHING));
          oledSetPriority(DISPLAY_PRIORITY_INFO, 1000);
        }
        weightCounterToApi = 0;
        weightSend = 0;
      }

    lastWeight = weight;

    // Wenn ein Tag erkannt wurde und das Gewicht stabil ist (4+ seconds), an FilaMan senden
    if (weightCounterToApi > 3 && weightSend == 0 && nfcReaderState == NFC_READ_SUCCESS && tagProcessed == false)
    {
      tagProcessed = true;

      // Check if it's a Bambu tag - if so, send only UUID without spoolId
      if (isBambuTag) {
        sendWeightAsync(0, activeTagUuid, weight);
        Serial.println("Bambu weight queued for FilaMan (UUID only)");
      } else {
        // Normal NTAG: send spoolId + UUID
        int sId = activeSpoolId.toInt();
        sendWeightAsync(sId, activeTagUuid, weight);
        Serial.println("Weight queued for FilaMan");
      }
      weightSend = 1;

      // Feedback to user
      oledShowProgressBar(3, 4, tr(STR_SPOOL_TAG), tr(STR_SENDING));
      oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);
    }

    // Handle successful tag write
    if (nfcReaderState == NFC_WRITE_SUCCESS && tagProcessed == false)
    {
      tagProcessed = true;

      // Only send weight if a valid spoolId exists (spool tag, not location tag)
      if (activeSpoolId.length() > 0 && activeSpoolId != "0") {
        int sId = activeSpoolId.toInt();
        sendWeightAsync(sId, activeTagUuid, weight);
        weightSend = 1;
        Serial.println("Weight queued for FilaMan after spool tag write");

        // Feedback to user
        oledShowProgressBar(3, 4, tr(STR_TAG_WRITTEN), tr(STR_SENDING));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);
      } else {
        // Location tag written - no weight to send
        Serial.println("Location tag written successfully - no weight send needed");
      }
    }
  }
  }
  esp_task_wdt_reset();
}
