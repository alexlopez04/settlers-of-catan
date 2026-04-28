#pragma once
// =============================================================================
// config.h — Hardware constants and global settings for the unified
//            ESP32-C6-WROOM-1 board controller.
//
// Pinout (ESP32-C6 DevKitC-1):
//
//   GPIO6  — I²C SDA   (PCF8575 sensor expanders, 4.7 kΩ pull-ups to 3.3 V)
//   GPIO7  — I²C SCL    "
//   GPIO10 — WS2812B data out (FastLED, RMT-driven). Place a 74HCT1G125
//            level shifter or use SK6812 LEDs (3.3 V tolerant) since the
//            C6 outputs 3.3 V and most WS2812B want 5 V data.
//   GPIO0  — Analog noise source for RNG seeding (ADC1_CH0).
//
// Strapping pins to avoid: GPIO8, GPIO9, GPIO15. UART0 USB-CDC is on the
// USB-JTAG bridge — Serial logging works without burning extra GPIOs.
// =============================================================================

#include <stdint.h>

// ── Serial (USB CDC for log output) ──────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── Addressable LEDs (WS2812B) ──────────────────────────────────────────────
static constexpr int      LED_DATA_PIN    = 10;
static constexpr uint16_t TOTAL_LED_COUNT = 57;  // 19×2 + 9×1 + 10 spare

static constexpr uint8_t MAX_LEDS_PER_TILE = 4;
static constexpr uint8_t MAX_LEDS_PER_PORT = 2;

// ── I²C GPIO Expanders (PCF8575) — sensor input ─────────────────────────────
static constexpr int     I2C_SDA_PIN     = 6;
static constexpr int     I2C_SCL_PIN     = 7;
static constexpr uint32_t I2C_BUS_HZ     = 400000;     // C6 happily runs 400 kHz

static constexpr uint8_t EXPANDER_COUNT = 8;
static constexpr uint8_t EXPANDER_ADDRS[EXPANDER_COUNT] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
};
static constexpr uint8_t PINS_PER_EXPANDER = 16;

// ── Players / BLE ───────────────────────────────────────────────────────────
static constexpr uint8_t MAX_PLAYERS         = 4;
static constexpr uint8_t MIN_PLAYERS         = 1;
static constexpr uint8_t MAX_BLE_CONNECTIONS = 4;       // one BLE central per seat
// MTU sized to fit a worst-case BoardState in a single notification.
// NimBLE hard-cap is 517; 512 leaves 509 bytes of usable ATT payload.
static constexpr uint16_t BLE_MTU            = 512;

#define CATAN_BLE_DEVICE_NAME    "Catan-Board"
#define CATAN_BLE_SERVICE_UUID   "CA7A0001-CA7A-4C4E-8000-00805F9B34FB"
#define CATAN_BLE_STATE_UUID     "CA7A0002-CA7A-4C4E-8000-00805F9B34FB"  // notify (BoardState)
#define CATAN_BLE_INPUT_UUID     "CA7A0003-CA7A-4C4E-8000-00805F9B34FB"  // write  (PlayerInput)
#define CATAN_BLE_IDENTITY_UUID  "CA7A0004-CA7A-4C4E-8000-00805F9B34FB"  // write  (client_id)
#define CATAN_BLE_SLOT_UUID      "CA7A0005-CA7A-4C4E-8000-00805F9B34FB"  // read+notify (uint8 slot or 0xFF)

// ── Board Geometry (standard Catan) ─────────────────────────────────────────
static constexpr uint8_t TILE_COUNT   = 19;
static constexpr uint8_t PORT_COUNT   = 9;
static constexpr uint8_t VERTEX_COUNT = 54;
static constexpr uint8_t EDGE_COUNT   = 72;

// ── Timing ──────────────────────────────────────────────────────────────────
static constexpr uint32_t SENSOR_POLL_MS         = 20;     // 50 Hz sensor + FSM tick
static constexpr uint32_t STATE_BROADCAST_MS     = 200;    // BoardState notify cadence
static constexpr uint32_t HEARTBEAT_MS           = 5000;
static constexpr uint32_t SLOT_CLAIM_TIMEOUT_MS  = 4000;   // Identity must arrive in this window

// ── Demo Mode ────────────────────────────────────────────────────────────────
static constexpr bool     DEMO_MODE      = false;
static constexpr uint32_t DEMO_CYCLE_MS  = 2000;

// ── Game Rules ──────────────────────────────────────────────────────────────
static constexpr uint8_t  VP_TO_WIN      = 10;
static constexpr uint8_t  INITIAL_ROUNDS = 2;
static constexpr uint8_t  ROBBER_ROLL    = 7;
