// =============================================================================
// display_manager.cpp — SSD1306 128×64 OLED display (GME12864-11).
// I2C address: 0x3C.  Uses Adafruit SSD1306 + Adafruit GFX.
// At text size 1: 6px/char wide, 8px/char tall → 21 cols × 8 rows.
// =============================================================================

#include "display_manager.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdio.h>

namespace {
    Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

    // Pixel Y offset for a text-size-1 row (0-indexed).
    inline int16_t rowY(uint8_t row) { return (int16_t)(row * 8); }
}

namespace display {

void init() {
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println(F("[OLED] init failed"));
        return;
    }
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print(F("Settlers of Catan"));
    oled.display();
}

void clear() {
    oled.clearDisplay();
    oled.display();
}

void printRow(uint8_t row, const char* text) {
    if (row >= OLED_ROWS) return;
    // Erase the row
    oled.fillRect(0, rowY(row), OLED_WIDTH, 8, SSD1306_BLACK);
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, rowY(row));
    oled.print(text);
    oled.display();
}

void showTitle(const char* title) {
    printRow(0, title);
}

void showMessage(const char* line1, const char* line2) {
    printRow(1, line1);
    if (line2) {
        printRow(2, line2);
    }
}

void showPlayerTurn(uint8_t player_num, uint8_t total_players) {
    char buf[OLED_COLS + 1];
    snprintf(buf, sizeof(buf), "Player %u/%u turn", player_num + 1, total_players);
    printRow(0, buf);
}

void showDiceResult(uint8_t die1, uint8_t die2) {
    char buf[OLED_COLS + 1];
    snprintf(buf, sizeof(buf), "Dice: %u+%u=%u", die1, die2, die1 + die2);
    printRow(1, buf);
}

void showSetupPrompt(const char* prompt) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, rowY(0));
    oled.print(F("-- SETUP --"));
    oled.setCursor(0, rowY(1));
    oled.print(prompt);
    oled.display();
}

void showButtonBar(const char* left, const char* center, const char* right) {
    // Clear the button bar area (y=55..63) and draw a separator line at y=55.
    oled.fillRect(0, 55, OLED_WIDTH, 9, SSD1306_BLACK);
    oled.drawFastHLine(0, 55, OLED_WIDTH, SSD1306_WHITE);

    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    if (left && *left) {
        oled.setCursor(0, 56);
        oled.print(left);
    }
    if (center && *center) {
        int16_t cx = (int16_t)((OLED_WIDTH - (int16_t)(strlen(center) * 6)) / 2);
        if (cx < 0) cx = 0;
        oled.setCursor(cx, 56);
        oled.print(center);
    }
    if (right && *right) {
        int16_t rx = (int16_t)(OLED_WIDTH - (int16_t)(strlen(right) * 6));
        if (rx < 0) rx = 0;
        oled.setCursor(rx, 56);
        oled.print(right);
    }
    oled.display();
}

}  // namespace display
