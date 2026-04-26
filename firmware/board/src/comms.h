#pragma once
// =============================================================================
// comms.h — Unified BLE peripheral for the merged ESP32-C6 board controller.
//
// Single GATT service exposing four characteristics:
//
//   State    (READ + NOTIFY) — bare BoardState protobuf bytes
//   Input    (WRITE)         — bare PlayerInput protobuf bytes
//   Identity (WRITE)         — UTF-8 client_id string (claims a seat)
//   Slot     (READ + NOTIFY) — uint8 player_id (0..3) or 0xFF if unseated
//
// Identity must be written within SLOT_CLAIM_TIMEOUT_MS of the BLE
// connect; otherwise the central is disconnected. Once a seat is bound
// the Slot characteristic is notified so the app can render
// "You are Player N".
//
// BLE callbacks run on the NimBLE host task. Player inputs are handed to
// the game task via a FreeRTOS queue (poll() drains it on the main task).
// Presence changes set a flag the main task picks up the same way.
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include "catan_wire.h"

namespace comms {

// Called from the main task with each freshly authenticated PlayerInput.
typedef void (*InputHandler)(const catan_PlayerInput& input);

// Called from the main task when the connected-slot bitmask changes.
typedef void (*PresenceHandler)(uint8_t mask);

void init();

// Drain queued events. Both handlers are optional. Safe to call frequently.
void poll(InputHandler on_input, PresenceHandler on_presence);

// Push the latest BoardState to all subscribers. The bytes must be a
// fully-encoded nanopb body (no header). Cheap to call at high cadence —
// internally caches the value for late subscribers' READ requests and
// notifies once per call.
void broadcastBoardState(const uint8_t* payload, size_t len);

// Number of active BLE centrals.
uint8_t connectedCount();

// Bitmask of currently-occupied seats.
uint8_t connectedMask();

// Periodic housekeeping (Identity-claim timeout). Cheap; call from loop().
void tick();

struct Stats {
    uint32_t inputs_rx;       // accepted PlayerInput writes
    uint32_t inputs_dropped;  // bad decode / no slot / queue full
    uint32_t state_notified;  // BoardState notify calls
    uint32_t presence_events; // queued presence changes
    uint32_t connect_events;
    uint32_t disconnect_events;
};
const Stats& stats();

}  // namespace comms
