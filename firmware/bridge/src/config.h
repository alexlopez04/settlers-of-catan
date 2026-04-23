#pragma once
// =============================================================================
// config.h — Heltec V3 bridge configuration.
// =============================================================================

#include <stdint.h>

// ── Serial ───────────────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── UART link to Arduino Mega — uses HardwareSerial(1) on ESP32-S3 ──────────
// Cross-connect: Bridge TX(42) → Mega RX(19)  |  Mega TX(18) → Bridge RX(41)
//
// Keep this in lockstep with `BRIDGE_SERIAL_BAUD` on the Mega side. The line
// runs unshifted between 5 V Mega logic and a 3.3 V ESP32-S3 RX, so we stay
// well below 115200 to give the edges time to settle cleanly. See the Mega
// config for the full rationale.
static constexpr uint32_t MEGA_SERIAL_BAUD = 38400;
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
// Frequency chosen to sit in the upper half of the US ISM band where
// Meshtastic (default slot 20 ≈ 908.6 MHz, LongFast ≈ 903–908 MHz, popular
// MEDIUM_FAST slot 45 ≈ 913.1 MHz) is less active. 923.3 MHz falls cleanly
// between Meshtastic MEDIUM_FAST slots 86 (922.625) and 87 (923.375) so even
// if a neighbour is on that preset our 500 kHz passband doesn't overlap.
static constexpr float    LORA_FREQ_MHZ     = 923.3f;
static constexpr float    LORA_BW_KHZ       = 500.0f;   // widest BW
static constexpr uint8_t  LORA_SF           = 7;        // lowest SF → fastest
static constexpr uint8_t  LORA_CR           = 5;        // 4/5 coding rate
static constexpr uint8_t  LORA_SYNCWORD     = 0xCA;     // "CA"tan private network
static constexpr int8_t   LORA_POWER_DBM    = 14;
static constexpr uint16_t LORA_PREAMBLE_LEN = 8;
// Heltec WiFi LoRa 32 V3 gates the 32 MHz TCXO via SX1262 DIO3 at 1.8 V (see
// Heltec schematic / their RadioLib fork). RadioLib defaults to 1.6 V which
// is below the TCXO's guaranteed start-up voltage, so `begin()` returns OK
// but the PLL never locks cleanly and the radio silently fails to TX/RX.
// Keep this at 1.8 V unless the hardware actually has no TCXO.
static constexpr float    LORA_TCXO_V       = 1.8f;
static constexpr bool     LORA_USE_LDO      = false;   // Heltec V3 uses DCDC

// ── Players ─────────────────────────────────────────────────────────────────
static constexpr uint8_t MAX_PLAYERS = 4;

// ── Cadence ─────────────────────────────────────────────────────────────────
static constexpr uint32_t LORA_BROADCAST_MIN_MS = 150;  // minimum spacing between TX
static constexpr uint32_t OLED_REFRESH_MS       = 500;

// ── Buffering ───────────────────────────────────────────────────────────────
static constexpr uint8_t  PLAYER_INPUT_QUEUE = 8;   // ring buffer slots
