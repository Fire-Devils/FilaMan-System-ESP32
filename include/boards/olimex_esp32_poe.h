#ifndef BOARD_OLIMEX_ESP32_POE_H
#define BOARD_OLIMEX_ESP32_POE_H

#include <Arduino.h>

// =============================================================================
// Olimex ESP32-PoE Board Configuration
// =============================================================================
// 
// Hardware:
//   - ESP32-WROOM-32 (WiFi built-in)
//   - PoE Ethernet interface with PHY chip
//   - Different pin mapping than ESP32-DEV
//
// =============================================================================

// I2C Pins
static const uint8_t I2C_SDA = 13; // GPIO 13
static const uint8_t I2C_SCL = 14; // GPIO 14

// NFC PN532 pins 
static const uint8_t PN532_IRQ = 32; // GPIO 32
static const uint8_t PN532_RESET = 33; // GPIO 33

// HX711 Scale pins
static const uint8_t LOADCELL_DOUT_PIN = 35;  // GPIO 35
static const uint8_t LOADCELL_SCK_PIN = 4;   // GPIO 4

// TTP223 (Touch module) pins
static const uint8_t TTP223_PIN = 5; // GPIO 5

#endif // BOARD_OLIMEX_ESP32_POE_H
