#include "display.h"
#include "api.h"
#include <vector>
#include "icons.h"
#include "main.h"
#include <cstring>
#include "lang.h"
#include <Preferences.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool wifiOn = false;
#ifdef HAS_ETHERNET
bool ethOn = false;
#endif
bool iconToggle = false;

// Display priority management
static DisplayPriority currentPriority = DISPLAY_PRIORITY_NONE;
static unsigned long prioritySetTime = 0;
static unsigned long priorityMinDuration = 0;

// Display sleep management
static bool oledAsleep = false;
static unsigned long lastActivityMillis = 0;

void oledWake() {
    if (oledAsleep) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        oledAsleep = false;
    }
    lastActivityMillis = millis();
}

void oledSleep() {
    if (!oledAsleep) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        oledAsleep = true;
    }
}

void oledResetActivityTimer() {
    lastActivityMillis = millis();
    if (oledAsleep) {
        oledWake();
    }
}

void oledCheckSleep() {
    if (oledSleepTimeout == 0 || oledAsleep) return;
    if (millis() - lastActivityMillis >= (unsigned long)oledSleepTimeout * 1000UL) {
        oledSleep();
    }
}

bool isOledAsleep() {
    return oledAsleep;
}

void loadOledSleepTimeout() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, true);
    oledSleepTimeout = preferences.getUShort(NVS_KEY_OLED_SLEEP, 60);
    preferences.end();
    Serial.print("OLED sleep timeout loaded: ");
    Serial.println(oledSleepTimeout);
}

void saveOledSleepTimeout(uint16_t timeoutSecs) {
    oledSleepTimeout = timeoutSecs;
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, false);
    preferences.putUShort(NVS_KEY_OLED_SLEEP, timeoutSecs);
    preferences.end();
}

bool oledCanUpdate(DisplayPriority newPriority) {
    if (currentPriority == DISPLAY_PRIORITY_NONE) return true;
    // Higher priority can always preempt
    if (newPriority > currentPriority) return true;
    // Same or lower priority: only if minimum duration has elapsed
    unsigned long elapsed = millis() - prioritySetTime;
    return elapsed >= priorityMinDuration;
}

void oledSetPriority(DisplayPriority priority, unsigned long minDurationMs) {
    currentPriority = priority;
    prioritySetTime = millis();
    priorityMinDuration = minDurationMs;
}

void oledClearPriority() {
    currentPriority = DISPLAY_PRIORITY_NONE;
    prioritySetTime = 0;
    priorityMinDuration = 0;
}

DisplayPriority oledGetCurrentPriority() {
    // Auto-expire: if min duration has passed, priority is effectively NONE
    if (currentPriority != DISPLAY_PRIORITY_NONE) {
        unsigned long elapsed = millis() - prioritySetTime;
        if (elapsed >= priorityMinDuration) {
            currentPriority = DISPLAY_PRIORITY_NONE;
        }
    }
    return currentPriority;
}
void setupDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, true, false)) {
        Serial.printf("SSD1306 allocation failed");
        for (;;); // Stoppe das Programm, wenn das Display nicht initialisiert werden kann
    }
    display.setTextColor(WHITE);
    display.clearDisplay();

    // Load sleep timeout from NVS
    loadOledSleepTimeout();
    lastActivityMillis = millis();

    oledShowTopRow();
    oledShowProgressBar(0, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_DISPLAY_INIT));
}

void oledclearline() {
    display.fillRect(0, 0, SCREEN_WIDTH, 16, BLACK);
}

void oledcleardata() {
    display.fillRect(0, OLED_DATA_START, SCREEN_WIDTH, OLED_DATA_END - OLED_DATA_START, BLACK);
}

int oled_center_h(const String &text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (SCREEN_WIDTH - w) / 2;
}

int oled_center_v(const String &text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, OLED_DATA_START, &x1, &y1, &w, &h);
    // Zentrierung nur im Datenbereich zwischen OLED_DATA_START und OLED_DATA_END
    return OLED_DATA_START + ((OLED_DATA_END - OLED_DATA_START - h) / 2);
}

std::vector<String> splitTextIntoLines(const String &text, uint8_t textSize) {
    std::vector<String> lines;
    display.setTextSize(textSize);

    // Text in Wörter aufteilen
    std::vector<String> words;
    int pos = 0;
    while (pos < text.length()) {
        // Überspringe Leerzeichen am Anfang
        while (pos < text.length() && text[pos] == ' ') pos++;

        // Finde nächstes Leerzeichen
        int nextSpace = text.indexOf(' ', pos);
        if (nextSpace == -1) {
            // Letztes Wort
            if (pos < text.length()) {
                words.push_back(text.substring(pos));
            }
            break;
        }
        // Wort hinzufügen
        words.push_back(text.substring(pos, nextSpace));
        pos = nextSpace + 1;
    }

    // Wörter zu Zeilen zusammenfügen
    String currentLine = "";
    for (size_t i = 0; i < words.size(); i++) {
        String testLine = currentLine;
        if (currentLine.length() > 0) testLine += " ";
        testLine += words[i];

        // Prüfe ob diese Kombination auf die Zeile passt
        int16_t x1, y1;
        uint16_t lineWidth, h;
        display.getTextBounds(testLine, 0, OLED_DATA_START, &x1, &y1, &lineWidth, &h);

        if (lineWidth <= SCREEN_WIDTH) {
            // Passt noch in diese Zeile
            currentLine = testLine;
        } else {
            // Neue Zeile beginnen
            if (currentLine.length() > 0) {
                lines.push_back(currentLine);
                currentLine = words[i];
            } else {
                // Ein einzelnes Wort ist zu lang
                lines.push_back(words[i]);
            }
        }
    }

    // Letzte Zeile hinzufügen
    if (currentLine.length() > 0) {
        lines.push_back(currentLine);
    }

    return lines;
}

void oledShowMultilineMessage(const String &message, uint8_t size) {
    std::vector<String> lines;
    int maxLines = 3;  // Maximale Anzahl Zeilen für size 2

    // Erste Prüfung mit aktueller Größe
    lines = splitTextIntoLines(message, size);

    // Wenn mehr als maxLines Zeilen, reduziere Textgröße
    if (lines.size() > maxLines && size > 1) {
        size = 1;
        lines = splitTextIntoLines(message, size);
    }

    // Ausgabe
    display.setTextSize(size);
    int lineHeight = size * 8;
    int totalHeight = lines.size() * lineHeight;
    int startY = OLED_DATA_START + ((OLED_DATA_END - OLED_DATA_START - totalHeight) / 2);

    uint8_t lineDistance = (lines.size() == 2) ? 5 : 0;
    for (size_t i = 0; i < lines.size(); i++) {
        display.setCursor(oled_center_h(lines[i]), startY + (i * lineHeight) + (i == 1 ? lineDistance : 0));
        display.print(lines[i]);
    }

    display.display();
}

void oledDisplayText(const String &message, uint8_t size) {
    oledcleardata();
    display.setTextSize(size);
    display.setTextWrap(false);

    // Prüfe ob Text in eine Zeile passt
    int16_t x1, y1;
    uint16_t textWidth, h;
    display.getTextBounds(message, 0, 0, &x1, &y1, &textWidth, &h);

    // Text passt in eine Zeile?
    if (textWidth <= SCREEN_WIDTH) {
        display.setCursor(oled_center_h(message), oled_center_v(message));
        display.print(message);
        display.display();
    } else {
        oledShowMultilineMessage(message, size);
    }
}

void oledShowTopRow() {
    const u_int8_t rightmost_x = 108;
    const u_int8_t x_offset = 25;
    const u_int8_t symbol_width =16;
    const u_int8_t symbol_height =16;
    u_int8_t x_pos = rightmost_x;

    oledclearline();

    display.setTextSize(1);
    display.setCursor(0, 4);
    display.print("v");
    display.print(VERSION);

    iconToggle = !iconToggle;

    // draw symbols from right to left
    // Do not show status indicators during boot
    if(!booting){
        // WiFi
        x_pos = rightmost_x;
        if (wifiOn == 1) {
            display.drawBitmap(x_pos, 0, wifi_on , symbol_width, symbol_height, WHITE);
        #ifdef HAS_ETHERNET
        } else if (ethOn == 1) {
                display.drawBitmap(x_pos, 0, eth_on, symbol_width, symbol_height, WHITE);
        #endif
        } else {
            if(iconToggle){
                display.drawBitmap(x_pos, 0, wifi_on , symbol_width, symbol_height, WHITE);
                display.drawLine(x_pos, symbol_height-1, x_pos + symbol_width, 0, WHITE);
                display.drawLine(x_pos+1, symbol_height-1, x_pos+1 + symbol_width, 0, WHITE);
            }
        }

        // Backend
        x_pos -= x_offset;  // change x position to the left
        if (filamanConnected) {
            display.drawBitmap(x_pos, 0, bitmap_spoolman_on , symbol_width, symbol_height, WHITE);
        } else {
            if(iconToggle){
                display.drawBitmap(x_pos, 0, bitmap_spoolman_on , symbol_width, symbol_height, WHITE);
                display.drawLine(x_pos, symbol_height-1, x_pos + symbol_width, 0, WHITE);
                display.drawLine(x_pos+1, symbol_height-1, x_pos+1 + symbol_width, 0, WHITE);
            }
        }

        // Scale
        x_pos -= x_offset;  // change x position to the left
        if (scaleConnected) {
            display.drawBitmap(x_pos, 0, bitmap_scale_on , symbol_width, symbol_height, WHITE);
        } else {
            display.drawBitmap(x_pos, 0, bitmap_scale_on , symbol_width, symbol_height, WHITE);
            display.drawLine(x_pos-1, symbol_height-1, x_pos-1 + symbol_width, 0, WHITE);
            display.drawLine(x_pos, symbol_height-1, x_pos + symbol_width, 0, WHITE);
            display.drawLine(x_pos+1, symbol_height-1, x_pos+1 + symbol_width, 0, WHITE);
        }
    }

    display.display();
}

void oledShowIcon(const char* icon) {
    oledcleardata();

    uint16_t iconSize = OLED_DATA_END-OLED_DATA_START;
    uint16_t iconStart = (SCREEN_WIDTH - iconSize) / 2;

    if (strcmp(icon, "failed") == 0) {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_failed , iconSize, iconSize, WHITE);
    }
    else if (strcmp(icon, "success") == 0) {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_success , iconSize, iconSize, WHITE);
    }
    else if (strcmp(icon, "transfer") == 0) {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_transfer , iconSize, iconSize, WHITE);
    }
    else if (strcmp(icon, "loading") == 0) {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_loading , iconSize, iconSize, WHITE);
    }

    display.display();
}

void oledShowProgressBar(const uint8_t step, const uint8_t numSteps, const char* largeText, const char* statusMessage) {
    assert(step <= numSteps);

    // clear data and bar area
    display.fillRect(0, OLED_DATA_START, SCREEN_WIDTH, SCREEN_HEIGHT-16, BLACK);


    display.setTextWrap(false);
    display.setTextSize(2);
    display.setCursor(0, OLED_DATA_START+4);
    display.print(largeText);
    display.setTextSize(1);
    display.setCursor(0, OLED_DATA_END-SCREEN_PROGRESS_BAR_HEIGHT-10);
    display.print(statusMessage);

    const int barLength = ((SCREEN_WIDTH-2)*step)/numSteps;
    display.drawRoundRect(0, SCREEN_HEIGHT-SCREEN_PROGRESS_BAR_HEIGHT, SCREEN_WIDTH, 12, 6, WHITE);
    display.fillRoundRect(1, SCREEN_HEIGHT-SCREEN_PROGRESS_BAR_HEIGHT+1, barLength, 10, 6, WHITE);
    display.display();
}

void oledShowWeight(int16_t weight) {
    // Display Gewicht
    oledcleardata();
    display.setTextSize(3);
    display.setCursor(oled_center_h(String(weight)+" g"), OLED_DATA_START+10);
    display.print(weight);
    display.print(" g");
    display.display();
}

void oledShowRemainingWeight(uint16_t remainingWeight) {
    oledcleardata();
    display.setTextSize(1);
    display.setCursor(oled_center_h(tr(STR_WEIGHT_SENT_REST)), OLED_DATA_START + 4);
    display.print(tr(STR_WEIGHT_SENT_REST));

    display.setTextSize(3);
    String weightStr = String(remainingWeight) + " g";
    display.setCursor(oled_center_h(weightStr), OLED_DATA_START + 22);
    display.print(weightStr);
    display.display();
}

void oledShowConnectionError(const char* error, const String& ip) {
    oledcleardata();
    display.setTextSize(1);

    // Error Message
    display.setCursor(oled_center_h(error), OLED_DATA_START + 4);
    display.print(error);

    // IP Address
    display.setTextSize(1);
    display.setCursor(oled_center_h(ip), OLED_DATA_START + 24);
    display.print(ip);

    display.display();
}
