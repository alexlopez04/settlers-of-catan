#pragma once
// =============================================================================
// config.h — Hardware constants and global settings for the Mega 2560
//            central board controller.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
// Serial  (USB)  — debug log
// Serial1 (TX1=18 / RX1=19) — framed mega_link to the ESP32-C6 BLE hub
// Serial2 (TX2=16 / RX2=17) — cv_link to Raspberry Pi CV detector
static constexpr uint32_t SERIAL_BAUD  = 115200;
static constexpr uint32_t LINK_BAUD    = 115200;
static constexpr uint32_t CV_LINK_BAUD = 115200;

// ── Addressable LEDs (WS2812B) ──────────────────────────────────────────────
static constexpr uint8_t  LED_DATA_PIN    = 3;
static constexpr uint16_t TOTAL_LED_COUNT = 57;  // 19×2 + 9×1 + 10 spare

static constexpr uint8_t MAX_LEDS_PER_TILE = 4;
static constexpr uint8_t MAX_LEDS_PER_PORT = 2;

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
// CV frames arrive at ~2 Hz from the Pi.  The main loop polls at SENSOR_POLL_MS
// — most iterations the cv_link will have nothing new and return immediately.
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
