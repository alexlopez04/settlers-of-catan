#pragma once
// =============================================================================
// config.h — Hardware constants, board geometry, and global settings.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── Addressable LEDs (WS2812B) ──────────────────────────────────────────────
static constexpr uint8_t  LED_DATA_PIN    = 3;
static constexpr uint16_t TOTAL_LED_COUNT = 57;  // 19×2 + 9×1 + 10 spare

static constexpr uint8_t MAX_LEDS_PER_TILE = 4;
static constexpr uint8_t MAX_LEDS_PER_PORT = 2;

// ── I2C GPIO Expanders (PCF8574) ────────────────────────────────────────────
static constexpr uint8_t EXPANDER_COUNT = 8;
static constexpr uint8_t EXPANDER_ADDRS[EXPANDER_COUNT] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
};
static constexpr uint8_t PINS_PER_EXPANDER = 8;

// ── I2C Player Station Addresses ────────────────────────────────────────────
static constexpr uint8_t MAX_PLAYERS     = 4;
static constexpr uint8_t MIN_PLAYERS     = 1;
static constexpr uint8_t PLAYER_I2C_BASE = 0x10;  // Player 0 = 0x10 .. Player 3 = 0x13

// ── Board Geometry (standard Catan) ─────────────────────────────────────────
static constexpr uint8_t TILE_COUNT   = 19;
static constexpr uint8_t PORT_COUNT   = 9;
static constexpr uint8_t VERTEX_COUNT = 54;
static constexpr uint8_t EDGE_COUNT   = 72;

// ── Timing ──────────────────────────────────────────────────────────────────
static constexpr uint32_t SENSOR_POLL_MS    = 20;
static constexpr uint32_t COMM_INTERVAL_MS  = 100;   // How often to sync state to players
static constexpr uint32_t PLAYER_DETECT_MS  = 1000;  // Player detection poll interval

// ── Game Rules ──────────────────────────────────────────────────────────────
static constexpr uint8_t  VP_TO_WIN        = 10;
static constexpr uint8_t  INITIAL_ROUNDS   = 2;   // Two rounds of initial placement
static constexpr uint8_t  ROBBER_ROLL      = 7;
static constexpr uint8_t  NUM_RESOURCES    = 5;

// ── Communication Buffer ────────────────────────────────────────────────────
static constexpr uint16_t I2C_BUF_SIZE     = 256;
