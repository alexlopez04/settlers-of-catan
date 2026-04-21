#pragma once
// =============================================================================
// config.h — Heltec V3 bridge configuration.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── UART link to Arduino Mega — uses HardwareSerial(1) on ESP32-S3 ──────────
// Cross-connect: Bridge TX(42) → Mega RX(19)  |  Mega TX(18) → Bridge RX(41)
static constexpr uint32_t MEGA_SERIAL_BAUD = 115200;
static constexpr int      MEGA_SERIAL_RX   = 41;   // was Wire1 SDA
static constexpr int      MEGA_SERIAL_TX   = 42;   // was Wire1 SCL

// ── On-board OLED (SSD1306 128x64) — uses Wire ──────────────────────────────
static constexpr int OLED_SDA  = 17;
static constexpr int OLED_SCL  = 18;
static constexpr int OLED_RST  = 21;
static constexpr int OLED_VEXT = 36;   // active-low power enable for OLED / LoRa

// ── LoRa (SX1262) — Heltec V3 pinout ────────────────────────────────────────
static constexpr int LORA_NSS   = 8;
static constexpr int LORA_DIO1  = 14;
static constexpr int LORA_NRST  = 12;
static constexpr int LORA_BUSY  = 13;
static constexpr int LORA_MOSI  = 10;
static constexpr int LORA_MISO  = 11;
static constexpr int LORA_SCK   = 9;

// ── LoRa radio parameters (tuned for speed over range) ──────────────────────
static constexpr float    LORA_FREQ_MHZ     = 915.0f;
static constexpr float    LORA_BW_KHZ       = 500.0f;   // widest BW
static constexpr uint8_t  LORA_SF           = 7;        // lowest SF → fastest
static constexpr uint8_t  LORA_CR           = 5;        // 4/5 coding rate
static constexpr uint8_t  LORA_SYNCWORD     = 0xCA;     // "CA"tan private network
static constexpr int8_t   LORA_POWER_DBM    = 14;
static constexpr uint16_t LORA_PREAMBLE_LEN = 8;

// ── Players ─────────────────────────────────────────────────────────────────
static constexpr uint8_t MAX_PLAYERS = 4;

// ── Cadence ─────────────────────────────────────────────────────────────────
static constexpr uint32_t LORA_BROADCAST_MIN_MS = 150;  // minimum spacing between TX
static constexpr uint32_t OLED_REFRESH_MS       = 500;

// ── Buffering ───────────────────────────────────────────────────────────────
static constexpr uint8_t  PLAYER_INPUT_QUEUE = 8;   // ring buffer slots
