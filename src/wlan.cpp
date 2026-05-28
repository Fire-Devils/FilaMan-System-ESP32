#include <Arduino.h>
#include "wlan.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include "display.h"
#include "config.h"
#include "lang.h"

WiFiManager wm;
bool wm_nonblocking = false;
uint8_t wifiErrorCounter = 0;

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
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
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

void initWiFi() {
  // load Wifi settings
  wifiSettings();

  // WiFi-Events abonnieren, bevor autoConnect() läuft, damit auch frühe
  // Disconnects/Reconnects schon erfasst werden.
  WiFi.onEvent(onWiFiEvent);

  wm.setAPCallback(configModeCallback);

  wm.setSaveConfigCallback([]() {
    Serial.println("Configurations updated");
    ESP.restart();
  });

  if(wm_nonblocking) wm.setConfigPortalBlocking(false);
  //wm.setConfigPortalTimeout(320); // Portal nach 5min schließen
  // WiFiManager-Reconnect deaktiviert: der ESP32-Stack übernimmt das via
  // setAutoReconnect(true). Zwei parallele Reconnect-Mechanismen erzeugen
  // sonst einen Verbindungssturm, der den Router zu AUTH_EXPIRE-Kicks verleitet.
  wm.setWiFiAutoReconnect(false);
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
  if (WiFi.status() != WL_CONNECTED)
  {
    // setAutoReconnect(true) übernimmt die Wiederverbindung automatisch.
    // Hier nur Display-Update und Fehler zählen für den Neustart-Watchdog.
    oledDisplayText(tr(STR_WIFI_RECONNECTING));
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
