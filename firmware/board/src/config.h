#pragma once
// =============================================================================
// config.h — Hardware constants and global settings for the Mega 2560
//            central board controller.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
// Serial  (USB)  — debug log
// Serial1 (TX1=18 / RX1=19) — framed mega_link to the ESP32-C6 BLE hub
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t LINK_BAUD   = 115200;

// ── Addressable LEDs (WS2812B) ──────────────────────────────────────────────
static constexpr uint8_t  LED_DATA_PIN    = 3;
static constexpr uint16_t TOTAL_LED_COUNT = 57;  // 19×2 + 9×1 + 10 spare

static constexpr uint8_t MAX_LEDS_PER_TILE = 4;
static constexpr uint8_t MAX_LEDS_PER_PORT = 2;

// ── I2C GPIO Expanders (PCF8574) — sensor input ─────────────────────────────
// The Mega is the I²C master for the eight PCF8574 expanders only. The
// player BLE hub no longer shares this bus; it talks to the Mega over
// UART (Serial1). Bus speed remains 100 kHz with 4.7 kΩ pull-ups on the
// 3.3 V side of the level shifter.
static constexpr uint8_t EXPANDER_COUNT = 8;
static constexpr uint8_t EXPANDER_ADDRS[EXPANDER_COUNT] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
};
static constexpr uint8_t PINS_PER_EXPANDER = 8;

// ── Players ─────────────────────────────────────────────────────────────────
// Logical seat count. The single ESP32-C6 BLE hub assigns each connecting
// mobile a stable seat 0..(MAX_PLAYERS-1) by client_id and reports the
// occupancy mask to the Mega via PlayerPresence frames.
static constexpr uint8_t MAX_PLAYERS = 4;
static constexpr uint8_t MIN_PLAYERS = 1;

// ── Board Geometry (standard Catan) ─────────────────────────────────────────
static constexpr uint8_t TILE_COUNT   = 19;
static constexpr uint8_t PORT_COUNT   = 9;
static constexpr uint8_t VERTEX_COUNT = 54;
static constexpr uint8_t EDGE_COUNT   = 72;

// ── Timing ──────────────────────────────────────────────────────────────────
static constexpr uint32_t SENSOR_POLL_MS     = 20;
static constexpr uint32_t STATE_BROADCAST_MS = 200;   // BoardState push cadence

// ── Demo Mode ────────────────────────────────────────────────────────────────
// When true, skip all game logic and cycle each tile through random resource
// colors every DEMO_CYCLE_MS milliseconds.
static constexpr bool     DEMO_MODE      = false;
static constexpr uint32_t DEMO_CYCLE_MS  = 2000;

// ── Game Rules ──────────────────────────────────────────────────────────────
static constexpr uint8_t  VP_TO_WIN      = 10;
static constexpr uint8_t  INITIAL_ROUNDS = 2;
static constexpr uint8_t  ROBBER_ROLL    = 7;
