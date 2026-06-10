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

#endif