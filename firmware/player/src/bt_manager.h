#pragma once
// =============================================================================
// bt_manager.h — BLE GATT server for the Catan Player Station
//
// Provides a Bluetooth Low Energy interface that gives mobile devices full
// control parity with the physical buttons on the station.
//
// ── Service layout ───────────────────────────────────────────────────────────
//
//   Service UUID (128-bit):
//     CA7A0001-CA7A-4C4E-8000-00805F9B34FB
//
//   GameState characteristic  CA7A0002-CA7A-4C4E-8000-00805F9B34FB
//     Properties : READ | NOTIFY
//     Payload    : raw NanoPB-encoded BoardToPlayer message (no framing)
//     Usage      : Mobile reads the current game state at any time, and
//                  subscribes (CCCD enable) to receive automatic updates
//                  whenever the board sends a new state.
//
//   Command characteristic    CA7A0003-CA7A-4C4E-8000-00805F9B34FB
//     Properties : WRITE | WRITE_NO_RSP
//     Payload    : raw NanoPB-encoded PlayerToBoard message (no framing)
//     Usage      : Mobile writes this characteristic to send a button press
//                  or a semantic PlayerAction to the station.  The station
//                  processes the command identically to a physical button.
//
// ── Wire format (BLE) ────────────────────────────────────────────────────────
//   BLE characteristics carry raw NanoPB bytes with NO extra framing (ATT
//   packets already convey length).  This differs from the I2C path which
//   uses a [0xCA][len] header for robustness.
//
// ── Advertising ──────────────────────────────────────────────────────────────
//   Device name : "Catan-P1" … "Catan-P4"  (set via player_id at init)
//   Advertised service UUID included in the AD records.
//
// ── Thread safety ────────────────────────────────────────────────────────────
//   All public functions are safe to call from any FreeRTOS task.
//   bt_manager_notify_state() may be called from the main application loop;
//   it internally serialises access with a mutex.
// =============================================================================

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "proto/catan.pb.h"

// ── Command callback ─────────────────────────────────────────────────────────
//
// Called from the NimBLE host task whenever a mobile client writes to the
// Command characteristic.  The callback receives the fully decoded
// PlayerToBoard message.
//
// Implementors MUST be quick (no blocking I/O); queue work if needed.
typedef void (*bt_command_cb_t)(const catan_PlayerToBoard *cmd);

// ── Public API ────────────────────────────────────────────────────────────────

// Initialise the NimBLE stack, register the GATT service, and begin
// advertising.  Call once from app_main before the main loop.
//
//   player_id  : 0–3.  Sets the advertised device name and the initial
//                game-state characteristic value.
//   on_command : Callback invoked when a mobile client sends a command.
//                Pass NULL to ignore incoming commands.
void bt_manager_init(uint8_t player_id, bt_command_cb_t on_command);

// Serialise state and push a GATT notification to every subscribed client.
// Silently skips if no client is connected or subscriptions are inactive.
// Safe to call from any task.
void bt_manager_notify_state(const catan_BoardToPlayer *state);

// Returns true when at least one BLE central is connected.
bool bt_manager_connected(void);

#ifdef __cplusplus
}
#endif
