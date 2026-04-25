#pragma once
// =============================================================================
// cv_link.h — Framed UART link from the Raspberry Pi CV detector (Mega side).
//
// Uses Serial2 (TX2=16 / RX2=17) at CV_LINK_BAUD (115200).
// A level shifter is required between the Pi's 3.3 V UART and the Mega's 5 V.
//
// The Pi sends CATAN_MSG_CV_BOARD_STATE (0x04) frames at ~2 Hz.  Each frame
// carries a 63-byte nibble-packed snapshot of every vertex and edge owner.
//
// This module is receive-only; the Mega does not send anything to the Pi.
// =============================================================================

#include <stdint.h>
#include "config.h"

namespace cv_link {

// Initialise Serial2 and clear internal state.  Called from sensor::init().
void init();

// Drain Serial2 and parse incoming bytes.  Call every loop iteration from
// sensor::poll().  O(bytes_available) — returns immediately if the buffer
// is empty.
void poll();

// True if poll() has decoded at least one complete, valid CV frame since the
// last call to getLatest().  Cleared when getLatest() is called.
bool hasNewFrame();

// Copy the latest decoded ownership arrays into the caller-supplied buffers.
// Clears the hasNewFrame() flag.
//   vertex_owners[v] : player id 0-3, or 0xFF if empty (NO_PLAYER)
//   edge_owners[e]   : player id 0-3, or 0xFF if empty (NO_PLAYER)
// Returns false if no frame has ever been received (buffers are unchanged).
bool getLatest(uint8_t vertex_owners[VERTEX_COUNT],
               uint8_t edge_owners[EDGE_COUNT]);

struct Stats {
    uint32_t rx_bytes;
    uint32_t rx_frames;
    uint32_t rx_bad_crc;
    uint32_t rx_bad_magic;
    uint32_t rx_overruns;
};
const Stats& stats();

}  // namespace cv_link
