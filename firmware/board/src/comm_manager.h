#pragma once
// =============================================================================
// comm_manager.h — UART serial link to the ESP32 bridge (Serial1).
//
// Speaks the v4 wire protocol: every payload is a framed catan_Envelope.
//
// Downstream (board → bridge → players → mobile):
//     Envelope { BoardState | Ack | Nack }
// Upstream   (mobile → player → bridge → board):
//     Envelope { PlayerInput | SyncRequest }
//
// Reliability (Phase 1):
//   - Outgoing BoardState broadcasts are reliable=false (next tick wins).
//   - Outgoing Acks are reliable=false.
//   - Incoming reliable envelopes are deduplicated by (sender_id, seq) and
//     auto-Acked inside pollPlayerInput().
// =============================================================================

#include <stdint.h>
#include "proto/catan.pb.h"

namespace comm {

void init();

// Send the current BoardState snapshot (wrapped in an Envelope) to the bridge.
// Fills in proto_version, sender_id=BOARD, a fresh sequence_number, and
// timestamp_ms automatically. Returns true on successful transmit.
bool sendBoardState(const catan_BoardState& state);

// Low-level: wrap the caller-supplied body in an envelope (caller sets
// which_body and the body contents; this function fills in header fields
// and transmits). `reliable` is copied into the envelope.
bool sendEnvelopeBody(catan_Envelope& env, bool reliable);

// Convenience: transmit an Ack addressed to (to_sender, seq).
bool sendAck(uint32_t to_sender, uint32_t seq);

// Poll the bridge for one pending PlayerInput.
// Returns true iff a valid, non-duplicate PlayerInput envelope arrived and
// was successfully acked (if reliable). `out` is populated with the input
// and `out_sender` with the originating envelope's sender_id.
bool pollPlayerInput(catan_PlayerInput& out, uint32_t& out_sender);

// Diagnostics — counters bumped by the comm manager for logging / heartbeats.
struct Stats {
    uint32_t tx_boardstate;   // BoardState envelopes transmitted
    uint32_t tx_ack;          // Acks transmitted
    uint32_t tx_bytes;        // total bytes pushed to Serial1 TX
    uint32_t rx_bytes;        // total bytes drained from Serial1 RX
    uint32_t rx_frames_ok;    // valid envelopes decoded
    uint32_t rx_frames_bad;   // decode / framing / validation failures
    uint32_t rx_dups;         // duplicate reliable envelopes dropped
};

const Stats& stats();

}  // namespace comm
