#pragma once
// =============================================================================
// display.h — Thin wrapper around U8g2 for the Catan client OLED.
//
// Initialises the SSD1306 128×64 display over hardware I²C and exposes
// simple text/graphic primitives used by the UI layer.
//
// All drawing goes to an internal page buffer; call commit() once per frame
// to push it to the screen.
// =============================================================================

#include <stdint.h>
#include <U8g2lib.h>

namespace display {

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Initialise the display (Wire, I²C address, pins from config.h).
// Must be called once in setup() before any drawing.
void init();

// ── Frame management ─────────────────────────────────────────────────────────

// Begin building a new frame. Call at the start of your redraw function.
void beginFrame();

// Flush the completed frame to the OLED.
void commitFrame();

// ── Drawing primitives ────────────────────────────────────────────────────────

static constexpr uint8_t W = 128;
static constexpr uint8_t H = 64;

// Draw a null-terminated string at pixel (x, y) with the specified font.
// Fonts are U8g2 font pointers (e.g. u8g2_font_5x7_tr).
void drawStr(uint8_t x, uint8_t y, const char* text, const uint8_t* font);

// Draw a horizontal divider line.
void drawHLine(uint8_t y);

// Draw a filled rectangle (e.g. for a selection highlight).
void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

// Draw a progress / indicator bar.
// `filled` is the number of filled segments out of `total`.
void drawProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                     uint8_t filled, uint8_t total);

// Expose the underlying U8g2 instance for advanced use.
U8G2& raw();

}  // namespace display
