#pragma once
// =============================================================================
// mega_link.h — Framed UART mega_link to the ESP32-C6 BLE hub (Mega side).
//
// Identical wire format to firmware/hub/src/mega_link.h. The Mega uses Serial1
// (TX1=18, RX1=19) at 115200 8N1 with a level shifter to the 3.3 V hub.
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include "catan_wire.h"

namespace mega_link {

typedef void (*FrameHandler)(uint8_t type, const uint8_t* payload, uint16_t len);

void init(FrameHandler on_frame);
void poll();
bool send(uint8_t type, const uint8_t* payload, uint16_t payload_len);

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
