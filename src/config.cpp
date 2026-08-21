#include "config.h"

// ################### Config Bereich Start
// ***** PN532 (RFID)
//#define PN532_SCK   18
//#define PN532_MOSI  23
//#define PN532_SS    5
//#define PN532_MISO  19
#if CONFIG_IDF_TARGET_ESP32S3
const uint8_t PN532_IRQ = 15;
const uint8_t PN532_RESET = 16;
#else
const uint8_t PN532_IRQ = 32;
const uint8_t PN532_RESET = 33;
#endif
// ***** PN532

// ***** HX711 (Waage)
// HX711 circuit wiring
#if CONFIG_IDF_TARGET_ESP32S3
const uint8_t LOADCELL_DOUT_PIN = 19;
const uint8_t LOADCELL_SCK_PIN = 20;
#else
const uint8_t LOADCELL_DOUT_PIN = 16;
const uint8_t LOADCELL_SCK_PIN = 17;
#endif
const uint8_t calVal_eepromAdress = 0;
const uint16_t SCALE_LEVEL_WEIGHT = 500;
// ***** HX711

// ***** TTP223 (Touch Sensor)
// TTP223 circuit wiring
#if CONFIG_IDF_TARGET_ESP32S3
const uint8_t TTP223_PIN = 7;
#else
const uint8_t TTP223_PIN = 25;
#endif
// ***** TTP223


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

// ***** Task Prios
uint8_t rfidTaskCore = 1;
uint8_t rfidTaskPrio = 1;

uint8_t rfidWriteTaskPrio = 1;

uint8_t scaleTaskCore = 0;
uint8_t scaleTaskPrio = 1;
// ***** Task Prios
