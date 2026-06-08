#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/**
 * @file board_config.h
 * @brief Board configuration include file
 */

// =============================================================================
// 1. Stringification Macro Helpers (Fixes the "too many decimal points" error)
// =============================================================================
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifdef VERSION_TOKEN
  #define VERSION TOSTRING(VERSION_TOKEN)         // Turns 3.3.2 into "3.3.2"
#endif

#ifdef TOOLDVERSION_TOKEN
  #define TOOLDVERSION TOSTRING(TOOLDVERSION_TOKEN) // Turns 3.2.0 into "3.2.0"
#endif

// =============================================================================
// 2. Hardware Target Decoupling
// =============================================================================
#if defined(ESP32_DEV)
    // ESP32-DEV Board
    #include "boards/esp32_dev.h"
    #define BOARD_IDENTIFIER "ESP32_DEV"

#elif defined(OLIMEX_ESP32_POE)
    // Olimex ESP32-PoE Board
    #include "boards/olimex_esp32_poe.h"
    #define BOARD_IDENTIFIER "OLIMEX_ESP32_POE"

#else
    // Throw an explicit build error if an environment lacks a target flag
    #error "No valid board target defined in platformio.ini! Ensure either -DESP32_DEV or -DOLIMEX_ESP32_POE is set."
#endif

// Automatically enable ethernet support if the board defines a PHY type
#ifdef ETH_PHY_TYPE
    #define HAS_ETHERNET
#endif

// // Shared pin references redeclared as extern to be defined in respective board headers
// extern const uint8_t I2C_SDA;
// extern const uint8_t I2C_SCL;
extern const uint8_t PN532_IRQ;
extern const uint8_t PN532_RESET;
extern const uint8_t LOADCELL_DOUT_PIN;
extern const uint8_t LOADCELL_SCK_PIN;
extern const uint8_t TTP223_PIN;

#endif // BOARD_CONFIG_H