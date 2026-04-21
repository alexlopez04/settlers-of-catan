#pragma once
// =============================================================================
// comm_manager.h — UART serial link to the ESP32 bridge (Serial1).
//
// Hardware: Mega Serial1  TX=18 → Bridge RX  |  Bridge TX → Mega RX=19
//
// Downstream (board → bridge → players → mobile):  BoardState
// Upstream   (mobile → player → bridge → board):   PlayerInput
//
// Frame format (see catan_wire.h):  [0xCA] [len] [nanopb payload]
//
// The bridge pushes PlayerInput frames as soon as they arrive from LoRa.
// pollPlayerInput() drains the Serial1 RX buffer and returns the next
// fully-assembled frame; call it in a tight loop until it returns false.
// =============================================================================

#include <stdint.h>
#include "proto/catan.pb.h"

namespace comm {

void init();

// Push the current BoardState snapshot to the bridge.
// Returns true if the frame was delivered successfully over I2C.
bool sendBoardState(const catan_BoardState& state);

// Poll the bridge for one pending PlayerInput frame.
// Returns true if a valid frame was received (and decoded into `out`).
// Returns false when there is no pending input or decoding failed.
bool pollPlayerInput(catan_PlayerInput& out);

}  // namespace comm
