#pragma once
// =============================================================================
// config.h — Catan handheld client configuration.
//
// All pin numbers, timing constants, and BLE UUIDs live here.
// Adjust pin assignments to match your physical wiring before building.
// =============================================================================

#include <stdint.h>

// ── Serial (USB-CDC, for debug logs) ─────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── I²C (OLED display) ───────────────────────────────────────────────────────
//
// Default Arduino-ESP32 I²C pins for the ESP32-C6.
// Override if your carrier board routes them differently.
static constexpr int OLED_SDA_PIN  = 6;
static constexpr int OLED_SCL_PIN  = 7;
static constexpr uint8_t OLED_I2C_ADDR = 0x3C;  // Most SSD1306 modules

// ── Buttons (active-LOW, internal pull-up enabled) ───────────────────────────
//
//   BTN_A — Left   : navigate back / decrement value
//   BTN_B — Middle : confirm / send action
//   BTN_C — Right  : navigate forward / increment value
//
// Free GPIO pins on the C6-DevKitC-1 that do not conflict with USB-JTAG,
// I²C, or strapping pins. Change to suit your wiring.
static constexpr int BTN_A_PIN = 2;
static constexpr int BTN_B_PIN = 3;
static constexpr int BTN_C_PIN = 10;

// ── Button timing ────────────────────────────────────────────────────────────
static constexpr uint32_t BTN_DEBOUNCE_MS  = 30;    // Ignore edges shorter than this
static constexpr uint32_t BTN_LONG_MS      = 600;   // Hold duration for long-press

// ── BLE (central role — connects to the hub) ─────────────────────────────────
#define CATAN_BLE_HUB_NAME       "Catan-Board"
#define CATAN_BLE_SERVICE_UUID   "CA7A0001-CA7A-4C4E-8000-00805F9B34FB"
#define CATAN_BLE_STATE_UUID     "CA7A0002-CA7A-4C4E-8000-00805F9B34FB"  // notify
#define CATAN_BLE_INPUT_UUID     "CA7A0003-CA7A-4C4E-8000-00805F9B34FB"  // write
#define CATAN_BLE_IDENTITY_UUID  "CA7A0004-CA7A-4C4E-8000-00805F9B34FB"  // write
#define CATAN_BLE_SLOT_UUID      "CA7A0005-CA7A-4C4E-8000-00805F9B34FB"  // read+notify

// Time after connecting in which Identity must be written or the hub drops us.
static constexpr uint32_t IDENTITY_TIMEOUT_MS = 3000;

// How long to wait before re-scanning after a disconnect.
static constexpr uint32_t RESCAN_DELAY_MS = 2000;

// ── Display cadence ───────────────────────────────────────────────────────────
static constexpr uint32_t DISPLAY_REFRESH_MS = 100;  // 10 Hz redraw
