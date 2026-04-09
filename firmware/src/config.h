#pragma once
// =============================================================================
// config.h — Pin mappings, hardware constants, and board geometry sizes.
//
// Edit this file to match your physical wiring. All sensor-to-vertex,
// LED-to-tile, and adjacency mappings live in board_layout.h/cpp.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── Addressable LEDs (WS2812B) ──────────────────────────────────────────────
static constexpr uint8_t LED_DATA_PIN   = 3;
static constexpr uint16_t TOTAL_LED_COUNT = 57;  // 19 tiles * 2 + 9 ports + 10 spare

// ── Buttons ─────────────────────────────────────────────────────────────────
// Physical positions viewed from the front of the board.
// Buttons use internal pull-up resistors (INPUT_PULLUP); LOW = pressed.
static constexpr uint8_t BTN_LEFT_PIN   = 10;
static constexpr uint8_t BTN_CENTER_PIN = 9;
static constexpr uint8_t BTN_RIGHT_PIN  = 8;
static constexpr uint32_t DEBOUNCE_MS   = 50;

// ── I2C OLED Display (GME12864-11, SSD1306, 128×64) ────────────────────────
static constexpr uint8_t OLED_I2C_ADDR = 0x3C;
static constexpr uint8_t OLED_WIDTH    = 128;
static constexpr uint8_t OLED_HEIGHT   = 64;
static constexpr uint8_t OLED_ROWS     = 7;   // text-size-1: rows 0-6 (y=0-55); row 7 = button bar
static constexpr uint8_t OLED_COLS     = 21;  // text-size-1: 6px per char

// ── I2C GPIO Expanders (PCF8574 / MCP23017) ────────────────────────────────
// Base addresses for I2C GPIO expanders used for hall sensors.
// Each PCF8574 provides 8 GPIO pins; MCP23017 provides 16.
// Adjust count and addresses to match your hardware.
static constexpr uint8_t EXPANDER_COUNT       = 4;
static constexpr uint8_t EXPANDER_ADDRS[EXPANDER_COUNT] = {0x20, 0x21, 0x22, 0x23};
static constexpr uint8_t PINS_PER_EXPANDER    = 8;  // PCF8574 = 8, MCP23017 = 16

// ── Direct GPIO Hall Sensors ────────────────────────────────────────────────
// Mega digital pins used directly for hall sensors (before expanders).
// Listed in board_layout.cpp sensor tables — these are the available GPIO pins.
// Mega pins 22–53 are digital I/O; analogue A0–A15 (54–69) also usable.

// ── Board Geometry ──────────────────────────────────────────────────────────
static constexpr uint8_t TILE_COUNT   = 19;
static constexpr uint8_t PORT_COUNT   = 9;
static constexpr uint8_t VERTEX_COUNT = 54;
static constexpr uint8_t EDGE_COUNT   = 72;

// ── Timing ──────────────────────────────────────────────────────────────────
static constexpr uint32_t SENSOR_POLL_MS  = 20;
static constexpr uint32_t ALIVE_LOG_MS    = 5000;

// ── Players ─────────────────────────────────────────────────────────────────
static constexpr uint8_t MIN_PLAYERS = 2;
static constexpr uint8_t MAX_PLAYERS = 4;
