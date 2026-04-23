#pragma once
// =============================================================================
// config.h — Heltec V3 player station configuration.
// =============================================================================

#include <stdint.h>

#ifndef CATAN_PLAYER_ID
#define CATAN_PLAYER_ID 0
#endif
static_assert(CATAN_PLAYER_ID >= 0 && CATAN_PLAYER_ID < 4,
              "CATAN_PLAYER_ID must be 0..3 (set via platformio.ini build_flags)");

// ── Serial ───────────────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── OLED (SSD1306 128x64 via hardware I2C) ──────────────────────────────────
static constexpr int OLED_SDA  = 17;
static constexpr int OLED_SCL  = 18;
static constexpr int OLED_RST  = 21;
static constexpr int OLED_VEXT = 36;   // active-low power enable

// ── LoRa (SX1262 on Heltec V3) ──────────────────────────────────────────────
static constexpr int LORA_NSS  = 8;
static constexpr int LORA_DIO1 = 14;
static constexpr int LORA_NRST = 12;
static constexpr int LORA_BUSY = 13;
static constexpr int LORA_MOSI = 10;
static constexpr int LORA_MISO = 11;
static constexpr int LORA_SCK  = 9;

// Radio parameters — must match the bridge exactly.
// See firmware/bridge/src/config.h for rationale on the chosen frequency.
static constexpr float    LORA_FREQ_MHZ     = 923.3f;
static constexpr float    LORA_BW_KHZ       = 500.0f;
static constexpr uint8_t  LORA_SF           = 7;
static constexpr uint8_t  LORA_CR           = 5;
static constexpr uint8_t  LORA_SYNCWORD     = 0xCA;
static constexpr int8_t   LORA_POWER_DBM    = 14;
static constexpr uint16_t LORA_PREAMBLE_LEN = 8;
// Heltec WiFi LoRa 32 V3 gates the 32 MHz TCXO via SX1262 DIO3 at 1.8 V.
// MUST match the bridge exactly — see firmware/bridge/src/config.h.
static constexpr float    LORA_TCXO_V       = 1.8f;
static constexpr bool     LORA_USE_LDO      = false;   // Heltec V3 uses DCDC

// ── Optional physical buttons (active-low, internal pull-up) ────────────────
// Set to -1 to disable any of them. Default disabled: only the three
// test benches with real buttons soldered on should enable these, because
// a floating ESP32-S3 GPIO held only by the internal ~45kΩ pull-up is
// noisy enough (especially near the LoRa antenna during TX) to register
// spurious presses — which then fire reliable actions that spin the
// retry loop and flood the network.
static constexpr int BTN_LEFT   = -1;
static constexpr int BTN_CENTER = -1;
static constexpr int BTN_RIGHT  = -1;

static constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;

// ── BLE ─────────────────────────────────────────────────────────────────────
// Service: 0xCA7A0001-CA7A-4C4E-8000-00805F9B34FB
//   state (notify)   : 0xCA7A0002-... — current BoardState frame
//   input (write)    : 0xCA7A0003-... — PlayerInput frame from mobile
#define CATAN_BLE_SERVICE_UUID "CA7A0001-CA7A-4C4E-8000-00805F9B34FB"
#define CATAN_BLE_STATE_UUID   "CA7A0002-CA7A-4C4E-8000-00805F9B34FB"
#define CATAN_BLE_INPUT_UUID   "CA7A0003-CA7A-4C4E-8000-00805F9B34FB"

// ── Cadence ─────────────────────────────────────────────────────────────────
static constexpr uint32_t OLED_REFRESH_MS    = 250;
static constexpr uint32_t READY_REPEAT_MS    = 5000;  // resend READY while in lobby
