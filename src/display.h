#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"


extern Adafruit_SSD1306 display;
extern bool wifiOn;

void setupDisplay();
void oledclearline();
void oledcleardata();
int oled_center_h(const String &text);
int oled_center_v(const String &text);

void oledShowProgressBar(const uint8_t step, const uint8_t numSteps, const char* largeText, const char* statusMessage);

void oledShowWeight(uint16_t weight);
void oledShowRemainingWeight(uint16_t remainingWeight);
void oledDisplayText(const String &message, uint8_t size = 2);
void oledShowConnectionError(const char* error, const String& ip);
void oledShowTopRow();
void oledShowIcon(const char* icon);

#endif
