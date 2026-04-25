// =============================================================================
// display.cpp — U8g2 OLED driver implementation.
// =============================================================================

#include "display.h"
#include "config.h"

#include <Wire.h>

namespace display {

// ── Static U8g2 instance ──────────────────────────────────────────────────────
//
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C uses the full-frame buffer mode
// ("_F_") which keeps all 1024 bytes in RAM.  The ESP32-C6 has 512 KB of
// SRAM so this is not a concern.
//
// Constructor args: rotation, reset pin (U8X8_PIN_NONE = no reset line),
// SCL pin, SDA pin.  The arduino-esp32 HW-I2C driver selects the port from
// the SCL/SDA pins automatically.
//
// If your OLED uses a different controller (SH1106, SH1107, …) swap only
// this one line; all other display:: calls remain unchanged.

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_u8g2(
    U8G2_R0,
    /* reset= */ U8X8_PIN_NONE,
    /* clock= */ OLED_SCL_PIN,
    /* data=  */ OLED_SDA_PIN
);

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void init() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    g_u8g2.begin();
    g_u8g2.setFlipMode(0);
    g_u8g2.clearDisplay();
}

// ── Frame management ─────────────────────────────────────────────────────────

void beginFrame() {
    g_u8g2.clearBuffer();
}

void commitFrame() {
    g_u8g2.sendBuffer();
}

// ── Drawing primitives ────────────────────────────────────────────────────────

void drawStr(uint8_t x, uint8_t y, const char* text, const uint8_t* font) {
    g_u8g2.setFont(font);
    g_u8g2.setDrawColor(1);
    g_u8g2.drawStr(x, y, text);
}

void drawHLine(uint8_t y) {
    g_u8g2.setDrawColor(1);
    g_u8g2.drawHLine(0, y, W);
}

void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    g_u8g2.setDrawColor(1);
    g_u8g2.drawBox(x, y, w, h);
}

void drawProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                     uint8_t filled, uint8_t total) {
    g_u8g2.setDrawColor(1);
    g_u8g2.drawFrame(x, y, w, h);
    if (total > 0 && filled > 0) {
        const uint8_t inner = (uint8_t)(((uint16_t)(w - 2) * filled) / total);
        if (inner > 0) {
            g_u8g2.drawBox(x + 1, y + 1, inner, h - 2);
        }
    }
}

U8G2& raw() {
    return g_u8g2;
}

}  // namespace display
