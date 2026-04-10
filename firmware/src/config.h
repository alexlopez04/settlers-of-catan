#pragma once
// =============================================================================
// config.h — Hardware constants, board geometry sizes, and global settings.
//
// This file defines the physical hardware parameters that ALL other modules
// depend on.  For wiring details see:
//   pin_map.cpp   — sensor-to-pin assignments
//   led_map.cpp   — LED strip address assignments
//   board_topology.cpp — hex layout and adjacency (auto-generated)
//
// See docs/BOARD_CONFIG.md for a full configuration guide.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── Addressable LEDs (WS2812B) ──────────────────────────────────────────────
static constexpr uint8_t  LED_DATA_PIN    = 3;
static constexpr uint16_t TOTAL_LED_COUNT = 57;  // 19×2 + 9×1 + 10 spare

// Maximum LEDs assignable to a single tile or port (increase if needed).
static constexpr uint8_t MAX_LEDS_PER_TILE = 4;
static constexpr uint8_t MAX_LEDS_PER_PORT = 2;

// ── Buttons ─────────────────────────────────────────────────────────────────
static constexpr uint8_t  BTN_LEFT_PIN   = 10;
static constexpr uint8_t  BTN_CENTER_PIN = 9;
static constexpr uint8_t  BTN_RIGHT_PIN  = 8;
static constexpr uint32_t DEBOUNCE_MS    = 50;

// ── I2C OLED Display (GME12864-11, SSD1306, 128×64) ────────────────────────
static constexpr uint8_t OLED_I2C_ADDR = 0x3C;
static constexpr uint8_t OLED_WIDTH    = 128;
static constexpr uint8_t OLED_HEIGHT   = 64;
static constexpr uint8_t OLED_ROWS     = 7;
static constexpr uint8_t OLED_COLS     = 21;

// ── I2C GPIO Expanders (PCF8574 / MCP23017) ────────────────────────────────
// Up to 8 expanders for hall-effect sensors on tiles, vertices, and edges.
// Each PCF8574 provides 8 pins; MCP23017 provides 16.
// Addresses must match the hardware jumpers on each module.
static constexpr uint8_t EXPANDER_COUNT = 8;
static constexpr uint8_t EXPANDER_ADDRS[EXPANDER_COUNT] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
};
static constexpr uint8_t PINS_PER_EXPANDER = 8;  // PCF8574 = 8, MCP23017 = 16

// ── Board Geometry (standard Catan) ─────────────────────────────────────────
static constexpr uint8_t TILE_COUNT   = 19;
static constexpr uint8_t PORT_COUNT   = 9;
static constexpr uint8_t VERTEX_COUNT = 54;
static constexpr uint8_t EDGE_COUNT   = 72;

// ── Timing ──────────────────────────────────────────────────────────────────
static constexpr uint32_t SENSOR_POLL_MS = 20;
static constexpr uint32_t ALIVE_LOG_MS   = 5000;

// ── Players ─────────────────────────────────────────────────────────────────
static constexpr uint8_t MIN_PLAYERS = 2;
static constexpr uint8_t MAX_PLAYERS = 4;
