#ifndef BOARD_ESP32_S3_DEVKITC_H
#define BOARD_ESP32_S3_DEVKITC_H

#include <Arduino.h>

// I2C Pins (ESP32-S3 Arduino defaults)
static const uint8_t I2C_SDA = 8; // GPIO 8
static const uint8_t I2C_SCL = 9; // GPIO 9

// NFC PN532 pins (ESP32-S3)
static const uint8_t PN532_IRQ = 15; // GPIO 15
static const uint8_t PN532_RESET = 16; // GPIO 16

// HX711 Scale pins (ESP32-S3)
// Do NOT use GPIO19/20: they are the native USB lines (D-/D+) on the DevKitC-1
// and make HX711 detection unreliable. Keep in sync with README.md / README.de.md.
static const uint8_t LOADCELL_DOUT_PIN = 5; // GPIO 5
static const uint8_t LOADCELL_SCK_PIN = 6; // GPIO 6

// TTP223 Touch pin (ESP32-S3: GPIO 7)
static const uint8_t TTP223_PIN = 7; // GPIO 7

#endif // BOARD_ESP32_S3_DEVKITC_H
