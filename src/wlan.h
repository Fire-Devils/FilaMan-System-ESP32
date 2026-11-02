#ifndef WLAN_H
#define WLAN_H

#include <Arduino.h>

void initNetwork();
void checkWiFiConnection();

#ifdef HAS_ETHERNET
    extern bool ethOn;
#endif
bool networkConnected(); // returns ethOn || wifiOn
String getLocalIP(); //returns IP for Eth or WiFi.

// Sendeleistung in 0.1 dBm (85/110/130/150/170/195). Ungueltige Werte werden ignoriert.
bool isValidTxPowerDeciDbm(int16_t deciDbm);
void loadWifiTxPower();
void saveWifiTxPower(int16_t deciDbm);

#endif