#ifndef BOARD_ESP32_DEV_H
#define BOARD_ESP32_DEV_H

#include <Arduino.h>

// I2C Pins (ESP32-DEV)
static const uint8_t I2C_SDA = 21; // GPIO 21
static const uint8_t I2C_SCL = 22; // GPIO 22

// NFC PN532 pins (ESP32-DEV)
static const uint8_t PN532_IRQ = 32; // GPIO 32
static const uint8_t PN532_RESET = 33; // GPIO 33

// HX711 Scale pins (ESP32-DEV)  
static const uint8_t LOADCELL_DOUT_PIN = 16; // GPIO 16
static const uint8_t LOADCELL_SCK_PIN = 17; // GPIO 17

// TTP223 Touch pin (ESP32-DEV: GPIO 25)
static const uint8_t TTP223_PIN = 25; // GPIO 25

#endif // BOARD_ESP32_DEV_H
