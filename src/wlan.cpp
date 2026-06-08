#include <Arduino.h>
#include "wlan.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include "display.h"
#include "config.h"
#include "lang.h"

#ifdef HAS_ETHERNET
#include <ETH.h>
#endif

WiFiManager wm;
bool wm_nonblocking = false;
uint8_t wifiErrorCounter = 0;

bool networkConnected() {
    #ifdef HAS_ETHERNET
      return ethOn || (WiFi.status() == WL_CONNECTED);
    #else
      return WiFi.status() == WL_CONNECTED;
    #endif
}

void wifiSettings() {
    // Standard WiFi-Einstellungen für höchste Stabilität mit ESPAsyncWebServer
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("FilaMan");

    // ESP32-Core Auto-Reconnect aktivieren (unabhängig vom WiFiManager-Flag).
    // Sorgt dafür, dass der WiFi-Stack autonom direkt nach einem Disconnect
    // neu verbindet, ohne auf das 15s-Polling in checkWiFiConnection() warten zu müssen.
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    // KRITISCH: Power-Save deaktivieren für stabilen Webserver-Betrieb
    // Ohne dies geht WiFi in Modem-Sleep und verpasst Requests/Responses
    // Dies ist die Hauptursache für Verbindungsabbrüche bei regelmäßigen API-Calls
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Angemessene Sendeleistung (reduziert Hitzeprobleme)
    WiFi.setTxPower(WIFI_POWER_17dBm);

    // Hinweis: Kein esp_wifi_set_rssi_threshold() mehr.
    // Der vorherige Wert (-80 dBm) löste in Single-AP-Heimnetzen häufige
    // Disconnects aus, sobald das Signal in den normalen Wohnungs-Empfangsbereich
    // fiel. Ohne Mesh-/Roaming-Setup bringt der Threshold keinen Vorteil.
}

// Reagiert sofort auf Verbindungsverlust, statt bis zum nächsten
// checkWiFiConnection()-Tick zu warten. Das ESP32-Auto-Reconnect übernimmt
// die eigentliche Wiederherstellung; hier nur Logging + Display-Update.
void onNetworkEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      // Adds Ethernet Events
#ifdef HAS_ETHERNET
        case ARDUINO_EVENT_ETH_START:
            ETH.setHostname("FilaMan");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("ETH connected");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("ETH IP: ");
            Serial.println(ETH.localIP());
            ethOn = true;
            wifiErrorCounter = 0;
            oledShowTopRow();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("ETH disconnected");
            ethOn = false;
            oledShowTopRow();
            break;
#endif
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.printf("WiFi event: STA_DISCONNECTED, reason=%u\n",
                          info.wifi_sta_disconnected.reason);
            if (wifiOn) {
                wifiOn = false;
                oledShowTopRow();
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("WiFi event: STA_GOT_IP ");
            Serial.println(WiFi.localIP());
            wifiOn = true;
            wifiErrorCounter = 0;
            // Power-Save nach Reconnect erneut deaktivieren (wird vom Stack
            // gelegentlich zurückgesetzt).
            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
            oledShowTopRow();
            break;
        default:
            break;
    }
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  oledShowTopRow();
  oledDisplayText(tr(STR_WIFI_CONFIG_MODE));
}

void initNetwork() {
  // load Wifi settings
  wifiSettings();

  // Network Events abonnieren, bevor autoConnect() läuft, damit auch frühe
  // Disconnects/Reconnects schon erfasst werden.
  WiFi.onEvent(onNetworkEvent);

#ifdef HAS_ETHERNET
    // Important for ESP32-POE-ISO:
    // Keep PHY power disabled before ETH.begin().
    // The LAN8720A/LAN87xx PHY must not be powered before the ESP32 RMII clock
    // is available on ETH_CLK_PIN.
    pinMode(ETH_PHY_POWER, OUTPUT);
    digitalWrite(ETH_PHY_POWER, LOW);
    delay(200);

    // now lets start the ethernet driver. It will check for the link and if found, it will set ethOn = true in the event handler.
    ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);

    for (int i = 0; i < 50 && !ethOn; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ethOn) {
        Serial.println("ETH connected - skipping WiFiManager");
        oledShowTopRow();
        return; // ← skip WiFiManager entirely
    }
#endif

  wm.setAPCallback(configModeCallback);

  wm.setSaveConfigCallback([]() {
    Serial.println("Configurations updated");
    ESP.restart();
  });

  if(wm_nonblocking) wm.setConfigPortalBlocking(false);
  //wm.setConfigPortalTimeout(320); // Portal nach 5min schließen
  wm.setWiFiAutoReconnect(true);
  wm.setConnectTimeout(10);

  oledShowProgressBar(1, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_WIFI_INIT));

  //bool res = wm.autoConnect("FilaMan"); // anonymous ap
  if(!wm.autoConnect("FilaMan")) {
    Serial.println("Failed to connect or hit timeout");
    // ESP.restart();
    oledShowTopRow();
    oledDisplayText(tr(STR_WIFI_NOT_CONNECTED));
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
  #if defined(HAS_ETHERNET)
    // If ethernet is connected, WiFi reconnection is not needed
    if (ethOn) {
        wifiErrorCounter = 0;
        return;
    }
  #endif
  if (WiFi.status() != WL_CONNECTED)
  {
    // wifiOn-Flag und Display werden bereits vom WiFi-Event gesetzt.
    // Hier nur noch das Reconnect-Backup für Fälle, in denen das ESP32-
    // Auto-Reconnect aus irgendeinem Grund nicht greift (z. B. nach
    // bestimmten reason-codes wie AUTH_EXPIRE, NO_AP_FOUND).
    oledDisplayText(tr(STR_WIFI_RECONNECTING));

    // Sauberer Reconnect-Zyklus: erst trennen (ohne SSID zu löschen,
    // ohne WiFi-Off), kurz Pause, dann neu verbinden. Hilft bei Routern
    // mit Band-Steering, die einen halbtoten Session-State behalten.
    WiFi.disconnect(false, false);
    vTaskDelay(pdMS_TO_TICKS(50));
    WiFi.reconnect();

    wifiErrorCounter++;

    // Nach ca. 5 Minuten ohne Verbindung Neustart (bei WIFI_CHECK_INTERVAL
    // = 15s entspricht das 20 Fehlern).
    const uint8_t maxErrors = (uint8_t)(300000UL / WIFI_CHECK_INTERVAL);
    if (wifiErrorCounter >= maxErrors)
    {
      Serial.println("WiFi unable to reconnect for ~5 minutes. Restarting...");
      ESP.restart();
    }
  }
  else
  {
    // Connected: Fehlerzähler zurücksetzen. wifiOn wird vom GOT_IP-Event
    // gepflegt, daher hier nichts weiter zu tun.
    wifiErrorCounter = 0;
  }
}

String getLocalIP() {
#ifdef HAS_ETHERNET
    if (ethOn) return ETH.localIP().toString();
#endif
    return WiFi.localIP().toString();
}