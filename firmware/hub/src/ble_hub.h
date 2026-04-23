#pragma once
// =============================================================================
// ble_hub.h — Multi-connection BLE peripheral for the Catan player hub.
//
// Exposes one GATT service with four characteristics:
//   - State    (NOTIFY)        — bare BoardState protobuf bytes
//   - Input    (WRITE)         — bare PlayerInput protobuf bytes
//   - Identity (WRITE)         — UTF-8 client_id string (claims a seat)
//   - Slot     (READ + NOTIFY) — uint8 player_id (0..3) or 0xFF if no seat
//
// Identity must be written within SLOT_CLAIM_TIMEOUT_MS of the BLE
// connect; otherwise the hub disconnects the central. Once a seat is
// bound the hub notifies the Slot characteristic so the app can render
// "You are Player N".
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include "catan_wire.h"

namespace ble_hub {

// Callback invoked when a slot-bound peer writes a valid PlayerInput.
// `player_id` is authoritative — derived from the conn handle, NOT from
// the payload. The hub fills proto_version + player_id + client_id before
// invoking the callback so the consumer can forward straight to the mega_link.
typedef void (*InputHandler)(const catan_PlayerInput& input);

// Callback invoked whenever the connected slot mask changes (claim or
// release). The receiver typically wants to forward a PlayerPresence
// frame to the Mega.
typedef void (*PresenceHandler)();

void init(InputHandler on_input, PresenceHandler on_presence);

// Push the latest BoardState bytes to every connected central.
//   `frame_bytes` is the raw nanopb body (no UART envelope).
void broadcastBoardState(const uint8_t* payload, size_t len);

// Periodic housekeeping — call from loop() to enforce the
// SLOT_CLAIM_TIMEOUT_MS rule. Cheap, safe to call at high cadence.
void tick();

}  // namespace ble_hub
