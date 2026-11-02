#include "config.h"

// ################### Config Bereich Start
// ***** PN532 (RFID) - Pins moved to include/boards/<board>.h

// ***** HX711 (Waage) - Scale Defaults, Pins moved to include/boards/<board>.h
const uint8_t calVal_eepromAdress = 0;
const uint16_t SCALE_LEVEL_WEIGHT = 500;

// ***** TTP223 (Touch Sensor) - Moved to include/boards/<board>.h

// ***** Display
const uint8_t OLED_TOP_START = 0;
const uint8_t OLED_TOP_END = 16;
const uint8_t OLED_DATA_START = 17;
const uint8_t OLED_DATA_END = SCREEN_HEIGHT;

// ***** Display

// ***** Webserver
const uint8_t webserverPort = 80;
// ***** Webserver

// ***** API
String filamanUrl = "";
String filamanToken = "";
bool filamanRegistered = false;

// ***** API

// ***** Display Sleep
uint16_t oledSleepTimeout = 60; // Default 60 seconds (0 = disabled)

// ***** Display Sleep

// ***** WiFi
// Sendeleistung in 0.1 dBm. Default 19.5dBm = Maximum; nur senken, wenn der AP
// das Geraet rauswirft (reason=2/4) oder es direkt neben dem AP steht.
int16_t wifiTxPowerDeciDbm = 195;
// ***** WiFi

// ***** Task Prios
uint8_t rfidTaskCore = 1;
uint8_t rfidTaskPrio = 1;

uint8_t rfidWriteTaskPrio = 1;

uint8_t scaleTaskCore = 0;
uint8_t scaleTaskPrio = 1;

// ***** Task Prios
