#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// Display message priority levels (higher = more important)
enum DisplayPriority : uint8_t {
    DISPLAY_PRIORITY_NONE    = 0,  // No message active
    DISPLAY_PRIORITY_STATUS  = 1,  // Weight display, idle info
    DISPLAY_PRIORITY_INFO    = 2,  // Progress bars, boot messages
    DISPLAY_PRIORITY_ACTION  = 3,  // NFC feedback, user actions
    DISPLAY_PRIORITY_WARNING = 4   // Errors, connection issues
};

extern Adafruit_SSD1306 display;
extern bool wifiOn;
#ifdef HAS_ETHERNET
extern bool ethOn; // Only defined if HAS_ETHERNET is set
#endif

// Display priority management
bool oledCanUpdate(DisplayPriority newPriority);
void oledSetPriority(DisplayPriority priority, unsigned long minDurationMs);
void oledClearPriority();
DisplayPriority oledGetCurrentPriority();

void setupDisplay();
void oledclearline();
void oledcleardata();
int oled_center_h(const String &text);
int oled_center_v(const String &text);

// Display sleep management
void oledWake();
void oledSleep();
void oledResetActivityTimer();
void oledCheckSleep();
bool isOledAsleep();
void loadOledSleepTimeout();
void saveOledSleepTimeout(uint16_t timeoutSecs);

void oledShowProgressBar(const uint8_t step, const uint8_t numSteps, const char* largeText, const char* statusMessage);

void oledShowWeight(int16_t weight);
void oledShowRemainingWeight(uint16_t remainingWeight);
void oledDisplayText(const String &message, uint8_t size = 2);
void oledShowConnectionError(const char* error, const String& ip);
void oledShowTopRow();
void oledShowIcon(const char* icon);

#endif
