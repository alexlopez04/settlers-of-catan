#pragma once
// =============================================================================
// mega_link.h — Framed UART mega_link to the Mega.
//
// Wraps Serial1 in the [magic][type][len][payload][crc] envelope defined
// in catan_wire.h. Receive is byte-stream → callback whenever a complete,
// CRC-valid frame lands. Transmit is fire-and-forget (no ACK; loss is
// rare on a wired ~30 cm pair and a stale BoardState is simply replaced
// by the next 200 ms broadcast).
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include "catan_wire.h"

namespace mega_link {

// Callback invoked from poll() whenever a CRC-valid frame is received.
typedef void (*FrameHandler)(uint8_t type, const uint8_t* payload, uint8_t len);

void init(FrameHandler on_frame);

// Drain the UART RX buffer; invoke the callback for each complete frame.
// Call from loop() at high cadence (every iteration is fine).
void poll();

// Send a framed message. Returns true iff the frame was queued in the
// HardwareSerial TX buffer. Encoding/CRC failures and oversized payloads
// return false.
bool send(uint8_t type, const uint8_t* payload, uint8_t payload_len);

// Diagnostics.
struct Stats {
    uint32_t rx_bytes;
    uint32_t rx_frames;
    uint32_t rx_bad_crc;
    uint32_t rx_bad_magic;
    uint32_t rx_overruns;
    uint32_t tx_frames;
    uint32_t tx_dropped;
};
const Stats& stats();

}  // namespace mega_link
