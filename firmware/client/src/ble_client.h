#pragma once
// =============================================================================
// ble_client.h — BLE central role for the Catan handheld client.
//
// Behaviour:
//   1. Scan for the first advertisement from "Catan-Board".
//   2. Connect, request MTU 247, discover the Catan service.
//   3. Subscribe to State (notify) and Slot (notify) characteristics.
//   4. Write a stable Identity string (derived from our MAC address) so the
//      hub can assign / restore our player slot.
//   5. Forward decoded BoardState and Slot notifications to callbacks.
//   6. Expose sendInput() to transmit a PlayerInput to the hub.
//   7. On disconnect, wait RESCAN_DELAY_MS then restart scanning.
// =============================================================================

#include <stdint.h>
#include "proto/catan.pb.h"

namespace ble_client {

// ── Callbacks ─────────────────────────────────────────────────────────────────

typedef void (*StateHandler)(const catan_BoardState& state);
typedef void (*SlotHandler)(uint8_t slot);     // slot = 0..3 or 0xFF
typedef void (*ConnectHandler)();              // hub connected
typedef void (*DisconnectHandler)();           // hub disconnected

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void init(StateHandler   on_state,
          SlotHandler    on_slot,
          ConnectHandler on_connect,
          DisconnectHandler on_disconnect);

// Periodic work — call from loop() every iteration.
void tick();

// ── Outbound ─────────────────────────────────────────────────────────────────

// Encode and write a PlayerInput to the hub. No-op if not connected.
void sendInput(const catan_PlayerInput& input);

// ── Status ────────────────────────────────────────────────────────────────────

bool isConnected();

}  // namespace ble_client
