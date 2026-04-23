// =============================================================================
// mega_link.cpp — Framed UART mega_link to the Mega (see mega_link.h).
// =============================================================================

#include "link.h"
#include "config.h"
#include "catan_log.h"

#include <Arduino.h>

namespace {

// Receive state machine.
enum class RxState : uint8_t { WAIT_MAGIC, WAIT_TYPE, WAIT_LEN, WAIT_PAYLOAD, WAIT_CRC };

RxState              rx_state = RxState::WAIT_MAGIC;
uint8_t              rx_type  = 0;
uint8_t              rx_len   = 0;
uint8_t              rx_pos   = 0;
uint8_t              rx_buf[CATAN_MAX_PAYLOAD];
mega_link::FrameHandler   rx_cb    = nullptr;
mega_link::Stats          g_stats  = {};

void resetRx() { rx_state = RxState::WAIT_MAGIC; rx_pos = 0; }

void feed(uint8_t b) {
    g_stats.rx_bytes++;
    switch (rx_state) {
        case RxState::WAIT_MAGIC:
            if (b == CATAN_WIRE_MAGIC) {
                rx_state = RxState::WAIT_TYPE;
            } else {
                g_stats.rx_bad_magic++;
            }
            break;

        case RxState::WAIT_TYPE:
            rx_type  = b;
            rx_state = RxState::WAIT_LEN;
            break;

        case RxState::WAIT_LEN:
            rx_len = b;
            if (rx_len > CATAN_MAX_PAYLOAD) {
                g_stats.rx_overruns++;
                resetRx();
            } else if (rx_len == 0) {
                rx_state = RxState::WAIT_CRC;
            } else {
                rx_pos   = 0;
                rx_state = RxState::WAIT_PAYLOAD;
            }
            break;

        case RxState::WAIT_PAYLOAD:
            rx_buf[rx_pos++] = b;
            if (rx_pos >= rx_len) rx_state = RxState::WAIT_CRC;
            break;

        case RxState::WAIT_CRC: {
            uint8_t hdr[2] = { rx_type, rx_len };
            uint8_t crc    = catan_crc8(hdr, 2);
            // Continue CRC over payload.
            for (uint8_t i = 0; i < rx_len; ++i) {
                crc ^= rx_buf[i];
                for (uint8_t j = 0; j < 8; ++j) {
                    crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                        : (uint8_t)(crc << 1);
                }
            }
            if (crc == b) {
                g_stats.rx_frames++;
                if (rx_cb) rx_cb(rx_type, rx_buf, rx_len);
            } else {
                g_stats.rx_bad_crc++;
                LOGW("LINK", "bad CRC type=0x%02X len=%u got=0x%02X want=0x%02X",
                     rx_type, rx_len, b, crc);
            }
            resetRx();
            break;
        }
    }
}

}  // namespace

namespace mega_link {

const Stats& stats() { return g_stats; }

void init(FrameHandler on_frame) {
    rx_cb = on_frame;
    Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
    LOGI("LINK", "Serial1 @%lu rx=%d tx=%d",
         (unsigned long)LINK_BAUD, LINK_RX_PIN, LINK_TX_PIN);
}

void poll() {
    while (Serial1.available() > 0) {
        feed((uint8_t)Serial1.read());
    }
}

bool send(uint8_t type, const uint8_t* payload, uint8_t payload_len) {
    uint8_t frame[CATAN_MAX_FRAME];
    size_t n = catan_wire_pack(type, payload, payload_len, frame, sizeof(frame));
    if (n == 0) {
        g_stats.tx_dropped++;
        LOGW("LINK", "pack fail type=0x%02X len=%u", type, payload_len);
        return false;
    }
    size_t w = Serial1.write(frame, n);
    if (w != n) {
        g_stats.tx_dropped++;
        LOGW("LINK", "uart write short %u/%u", (unsigned)w, (unsigned)n);
        return false;
    }
    g_stats.tx_frames++;
    return true;
}

}  // namespace mega_link
