#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "wlan.h"
#include "config.h"
#include "website.h"
#include "api.h"
#include "display.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include "commonFS.h"

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

  // Start Display
  setupDisplay();

  // WiFiManager
  initWiFi();

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
  scaleTareRequest = true;

  // WDT initialisieren mit 10 Sekunden Timeout
  bool panic = true; // Wenn true, löst ein WDT-Timeout einen System-Panik aus
  esp_task_wdt_init(10, panic);

  booting = false;
  // Aktuellen Task (loopTask) zum Watchdog hinzufügen
  esp_task_wdt_add(NULL);
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

// ##### PROGRAM START #####
void loop() {
  unsigned long currentMillis = millis();

  // Überprüfe den Status des Touch Sensors
  if (touchSensorConnected && digitalRead(TTP223_PIN) == HIGH && currentMillis - lastButtonPress > debounceDelay) 
  {
    lastButtonPress = currentMillis;
    scaleTareRequest = true;
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
    // Clean up dead websocket clients periodically instead of on connect
    if(currentMillis % 10000 < 50) ws.cleanupClients(); 
  }

  // Periodic FilaMan heartbeat
  if (intervalElapsed(currentMillis, lastFilamanHeartbeatTime, FILAMAN_HEARTBEAT_INTERVAL)) 
  {
    sendHeartbeatAsync();
  }

  // If scale is not calibrated, only show a warning
  if (!scaleCalibrated) 
  {
    // Do not show the warning if the calibratin process is onging
    if(!scaleCalibrationActive){
      oledDisplayText("Scale not calibrated");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  } 
  else 
  {
    // Ausgabe der Waage auf Display
    // Block weight display during NFC write operations
    if(pauseMainTask == 0 && !nfcWriteInProgress)
    {
      // Use filtered weight for smooth display, but still check API weight for significant changes
      int16_t displayWeight = getFilteredDisplayWeight();
      if (mainTaskWasPaused || (weight != lastWeight && nfcReaderState == NFC_IDLE))
      {
        (displayWeight < 2) ? ((displayWeight < -2) ? oledDisplayText("!! -0") : oledShowWeight(0)) : oledShowWeight(displayWeight);
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
      } 
      else 
      {
        weightCounterToApi = 0;
        weightSend = 0;
      }
    }

    // reset weight counter after writing tag
    if (currentMillis - lastWeightReadTime >= weightReadInterval && nfcReaderState != NFC_IDLE && nfcReaderState != NFC_READ_SUCCESS)
    {
      weightCounterToApi = 0;
    }
    
    lastWeight = weight;

    // Wenn ein Tag erkannt wurde und das Gewicht stabil ist, an FilaMan senden
    if (weightCounterToApi > 3 && weightSend == 0 && nfcReaderState == NFC_READ_SUCCESS && tagProcessed == false) 
    {
      // set the current tag as processed to prevent it beeing processed again
      tagProcessed = true;
      
      // Get UID from nfcJsonData or similar (nfc.cpp should set it)
      // For now, use activeSpoolId if available
      int sId = activeSpoolId.toInt();
      sendWeightAsync(sId, activeTagUuid, weight);
      weightSend = 1;
      Serial.println("Weight queued for FilaMan");
    }

    // Handle successful tag write
    if (nfcReaderState == NFC_WRITE_SUCCESS && tagProcessed == false) 
    {
      tagProcessed = true;
      int sId = activeSpoolId.toInt();
      sendWeightAsync(sId, activeTagUuid, weight);
      weightSend = 1;
      Serial.println("Weight queued for FilaMan after tag write");
    }
  }
  
  esp_task_wdt_reset();
}
