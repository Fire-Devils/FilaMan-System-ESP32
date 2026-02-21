#include <Arduino.h>
#include "wlan.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include "display.h"
#include "config.h"

WiFiManager wm;
bool wm_nonblocking = false;
uint8_t wifiErrorCounter = 0;

void wifiSettings() {
    // Standard WiFi-Einstellungen für höchste Stabilität mit ESPAsyncWebServer
    WiFi.mode(WIFI_STA); 
    WiFi.setHostname("FilaMan");
    
    // Wir lassen das Power Management (Sleep) auf dem ESP-Standard.
    // Ein erzwungenes Ausschalten führt zu RF-Abstürzen bei vielen asynchronen HTTP-Requests.
    
    // Angemessene Sendeleistung (reduziert Hitzeprobleme)
    WiFi.setTxPower(WIFI_POWER_17dBm); // Set stable transmit power (default is 19.5 which gets hot)
  
    // Wir überlassen die Protokoll-Aushandlung (b/g/n) dem Treiber. Harte Vorgaben crashen unter Last.
    
    // Aktiviere WiFi-Roaming für bessere Stabilität
    esp_wifi_set_rssi_threshold(-80);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  oledShowTopRow();
  oledDisplayText("WiFi Config Mode");
}

void initWiFi() {
  // load Wifi settings
  wifiSettings();

  wm.setAPCallback(configModeCallback);

  wm.setSaveConfigCallback([]() {
    Serial.println("Configurations updated");
    ESP.restart();
  });

  if(wm_nonblocking) wm.setConfigPortalBlocking(false);
  //wm.setConfigPortalTimeout(320); // Portal nach 5min schließen
  wm.setWiFiAutoReconnect(true);
  wm.setConnectTimeout(10);

  oledShowProgressBar(1, 7, DISPLAY_BOOT_TEXT, "WiFi init");
  
  //bool res = wm.autoConnect("FilaMan"); // anonymous ap
  if(!wm.autoConnect("FilaMan")) {
    Serial.println("Failed to connect or hit timeout");
    // ESP.restart();
    oledShowTopRow();
    oledDisplayText("WiFi not connected Check Portal");
  } 
  else {
    wifiOn = true;

    //if you get here you have connected to the WiFi    
    Serial.println("connected...yeey :)");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    oledShowTopRow();
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) 
  {
    if (wifiOn) {
        Serial.println("WiFi connection lost. LwIP is auto-reconnecting...");
        wifiOn = false;
        oledShowTopRow();
        oledDisplayText("WiFi reconnecting");
    }
    
    wifiErrorCounter++;

    // Only restart after a significant time of no connection (e.g. 5 minutes)
    // Assuming WIFI_CHECK_INTERVAL is 60000ms (1 minute), 5 errors = 5 minutes
    if (wifiErrorCounter >= 5) 
    {
      Serial.println("WiFi unable to reconnect for 5 minutes. Restarting...");
      ESP.restart();
    }
  }
  else
  {
    if (!wifiOn) {
        Serial.println("WiFi reconnected.");
        wifiErrorCounter = 0;
        wifiOn = true;
        oledShowTopRow();
    } else {
        // Reset error counter if we are connected (just to be safe)
        wifiErrorCounter = 0;
    }
  }
}