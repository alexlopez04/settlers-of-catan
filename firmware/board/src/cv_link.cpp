// =============================================================================
// cv_link.cpp — Mega-side Serial2 link to the Raspberry Pi CV detector.
//
// Wire format is the standard CATAN_WIRE framing:
//   [0xCA magic][type:u8][len:u8][payload:len bytes][crc8:u8]
// CRC-8 (poly 0x07, init 0x00) covers [type, len, payload].
//
// Only CATAN_MSG_CV_BOARD_STATE (0x04) frames are processed here.
// All other type codes are discarded after logging.
// =============================================================================

#include "cv_link.h"
#include "config.h"
#include "catan_wire.h"
#include "catan_log.h"

#include <Arduino.h>
#include <string.h>

namespace {

// ── RX state machine ────────────────────────────────────────────────────────

enum class RxState : uint8_t {
    WAIT_MAGIC, WAIT_TYPE, WAIT_LEN, WAIT_PAYLOAD, WAIT_CRC
};

RxState rx_state = RxState::WAIT_MAGIC;
uint8_t rx_type  = 0;
uint8_t rx_len   = 0;
uint8_t rx_pos   = 0;
uint8_t rx_buf[CATAN_MAX_PAYLOAD];

// ── CV ownership buffers ─────────────────────────────────────────────────────

static uint8_t g_vertex_owners[VERTEX_COUNT];
static uint8_t g_edge_owners[EDGE_COUNT];
static bool    g_has_data     = false;  // true once first frame decoded
static bool    g_new_frame    = false;  // cleared by getLatest()

static cv_link::Stats g_stats = {};

void resetRx() {
    rx_state = RxState::WAIT_MAGIC;
    rx_pos   = 0;
}

// Decode a validated 63-byte CATAN_MSG_CV_BOARD_STATE payload into the
// ownership arrays.  Nibble encoding: 0x0-0x3 = player, 0xF = empty (0xFF).
static void decodeCvPayload(const uint8_t* payload, uint8_t len) {
    if (len != CATAN_CV_PAYLOAD_SIZE) {
        LOGW("CV", "bad payload length %u (expected %u)", (unsigned)len,
             (unsigned)CATAN_CV_PAYLOAD_SIZE);
        return;
    }

    // Vertices: bytes 0..26
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        uint8_t byte_idx = v >> 1;
        uint8_t nib = (v & 1)
            ? (payload[byte_idx] >> 4) & 0x0F
            : (payload[byte_idx]     ) & 0x0F;
        g_vertex_owners[v] = (nib <= 3) ? nib : 0xFF;
    }

    // Edges: bytes 27..62
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        uint8_t byte_idx = (uint8_t)((e >> 1) + CATAN_CV_VERTEX_BYTES);
        uint8_t nib = (e & 1)
            ? (payload[byte_idx] >> 4) & 0x0F
            : (payload[byte_idx]     ) & 0x0F;
        g_edge_owners[e] = (nib <= 3) ? nib : 0xFF;
    }

    g_has_data  = true;
    g_new_frame = true;
    g_stats.rx_frames++;
    LOGD("CV", "frame ok: decoded %u vertices, %u edges",
         (unsigned)VERTEX_COUNT, (unsigned)EDGE_COUNT);
}

// Process one received byte through the frame parser.
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
            // Recompute CRC over [type, len, payload].
            uint8_t hdr[2] = { rx_type, rx_len };
            uint8_t crc = catan_crc8(hdr, 2);
            for (uint8_t i = 0; i < rx_len; ++i) {
                crc ^= rx_buf[i];
                for (uint8_t j = 0; j < 8; ++j) {
                    crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                        : (uint8_t)(crc << 1);
                }
            }
            if (crc == b) {
                if (rx_type == CATAN_MSG_CV_BOARD_STATE) {
                    decodeCvPayload(rx_buf, rx_len);
                } else {
                    LOGW("CV", "unexpected type=0x%02X len=%u (discarded)",
                         (unsigned)rx_type, (unsigned)rx_len);
                }
            } else {
                g_stats.rx_bad_crc++;
                LOGW("CV", "bad CRC type=0x%02X len=%u", (unsigned)rx_type,
                     (unsigned)rx_len);
            }
            resetRx();
            break;
        }
    }
}

}  // anonymous namespace

namespace cv_link {

void init() {
    memset(g_vertex_owners, 0xFF, sizeof(g_vertex_owners));
    memset(g_edge_owners,   0xFF, sizeof(g_edge_owners));
    g_has_data  = false;
    g_new_frame = false;
    resetRx();
    Serial2.begin(CV_LINK_BAUD);
    LOGI("CV", "Serial2 @%lu (TX2=16 RX2=17)", (unsigned long)CV_LINK_BAUD);
}

void poll() {
    while (Serial2.available() > 0) {
        feed((uint8_t)Serial2.read());
    }
}

bool hasNewFrame() {
    return g_new_frame;
}

bool getLatest(uint8_t vertex_owners[VERTEX_COUNT],
               uint8_t edge_owners[EDGE_COUNT]) {
    if (!g_has_data) return false;
    memcpy(vertex_owners, g_vertex_owners, VERTEX_COUNT);
    memcpy(edge_owners,   g_edge_owners,   EDGE_COUNT);
    g_new_frame = false;
    return true;
}

const Stats& stats() { return g_stats; }

}  // namespace cv_link
