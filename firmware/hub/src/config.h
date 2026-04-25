#pragma once
// =============================================================================
// config.h — ESP32-C6 hub configuration.
// =============================================================================

#include <stdint.h>

// ── Logging serial (USB CDC) ───────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;

// ── UART mega_link to the Mega ──────────────────────────────────────────────────
//
// GPIO16 and GPIO17 are UART0 on the ESP32-C6 — they are wired to the
// USB-to-serial bridge (the same port used for Serial / log output). Using
// those pins for Serial1 would mirror every link frame back out the console.
// Use GPIO4 (TX1) and GPIO5 (RX1) instead — they are free on the DevKitC-1.
//
// Wires:
//   ESP32-C6 GPIO4  (TX1) -> Mega RX1 (pin 19) via 5V<->3V3 level shifter
//   ESP32-C6 GPIO5  (RX1) <- Mega TX1 (pin 18) via 5V<->3V3 level shifter
//   GND ↔ GND
static constexpr uint32_t LINK_BAUD = 115200;
static constexpr int      LINK_TX_PIN = 4;
static constexpr int      LINK_RX_PIN = 5;

// ── BLE ────────────────────────────────────────────────────────────────────
//
// Single advertised peripheral named "Catan-Board". Up to MAX_BLE_CONNECTIONS
// concurrent centrals (one per seat). The hub assigns each connecting
// device a player slot 0..3 by stable client_id (see player_slots.cpp).
#define CATAN_BLE_DEVICE_NAME    "Catan-Board"
#define CATAN_BLE_SERVICE_UUID   "CA7A0001-CA7A-4C4E-8000-00805F9B34FB"
#define CATAN_BLE_STATE_UUID     "CA7A0002-CA7A-4C4E-8000-00805F9B34FB"  // notify (BoardState)
#define CATAN_BLE_INPUT_UUID     "CA7A0003-CA7A-4C4E-8000-00805F9B34FB"  // write  (PlayerInput)
#define CATAN_BLE_IDENTITY_UUID  "CA7A0004-CA7A-4C4E-8000-00805F9B34FB"  // write  (client_id string)
#define CATAN_BLE_SLOT_UUID      "CA7A0005-CA7A-4C4E-8000-00805F9B34FB"  // read+notify (uint8 slot or 0xFF)

static constexpr uint8_t  MAX_PLAYERS         = 4;       // logical seat count
static constexpr uint8_t  MAX_BLE_CONNECTIONS = 4;       // concurrent centrals
// MTU must be ≥ CATAN_MAX_PAYLOAD + 3 (ATT opcode + handle overhead).
// iOS auto-negotiates up to 512; Android honours the requestMTU hint in the app.
// NimBLE hard-max is 517; 512 leaves 509 bytes of data per notification.
static constexpr uint16_t BLE_MTU             = 512;

// ── Cadence ────────────────────────────────────────────────────────────────
static constexpr uint32_t HEARTBEAT_MS         = 5000;
static constexpr uint32_t PRESENCE_RESEND_MS   = 1000;   // re-send presence even if unchanged
static constexpr uint32_t SLOT_CLAIM_TIMEOUT_MS = 4000;  // identity must arrive within this window
