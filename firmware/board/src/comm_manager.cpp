// =============================================================================
// comm_manager.cpp — UART serial link to the bridge (Serial1, 115200 baud).
//
// Wire framing: [0xCA magic][len : uint8][nanopb payload]
//
// Sending is straightforward — just write the framed bytes to Serial1.
//
// Receiving uses a small state machine:
//   HUNT  — discarding bytes until 0xCA is seen
//   LEN   — next byte is the payload length
//   BODY  — accumulating `len` payload bytes
// A complete frame triggers a decode attempt; any error resets to HUNT.
// =============================================================================

#include "comm_manager.h"
#include "config.h"
#include "catan_wire.h"
#include <Arduino.h>
#include <pb_encode.h>
#include <pb_decode.h>

namespace {

// ── TX ───────────────────────────────────────────────────────────────────────
uint8_t tx_buf[CATAN_MAX_FRAME];

// ── RX state machine ─────────────────────────────────────────────────────────
enum class RxState : uint8_t { HUNT, LEN, BODY };
uint8_t  rx_buf[CATAN_MAX_FRAME];
uint8_t  rx_pos    = 0;
uint8_t  rx_expect = 0;   // total frame bytes expected (HEADER + payload)
RxState  rx_state  = RxState::HUNT;

}  // namespace

namespace comm {

void init() {
    Serial1.begin(BRIDGE_SERIAL_BAUD);
    Serial.println(F("[COMM] Serial1 bridge link started"));
}

bool sendBoardState(const catan_BoardState& state) {
    pb_ostream_t os = pb_ostream_from_buffer(tx_buf + CATAN_FRAME_HEADER,
                                             sizeof(tx_buf) - CATAN_FRAME_HEADER);
    if (!pb_encode(&os, catan_BoardState_fields, &state)) {
        Serial.print(F("[COMM] encode BoardState failed: "));
        Serial.println(PB_GET_ERROR(&os));
        return false;
    }
    if (os.bytes_written > CATAN_MAX_PAYLOAD) {
        Serial.print(F("[COMM] payload too big: "));
        Serial.println(os.bytes_written);
        return false;
    }

    tx_buf[0] = CATAN_WIRE_MAGIC;
    tx_buf[1] = (uint8_t)os.bytes_written;
    const size_t total = CATAN_FRAME_HEADER + os.bytes_written;
    Serial1.write(tx_buf, total);
    return true;
}

bool pollPlayerInput(catan_PlayerInput& out) {
    // Drain whatever bytes have arrived from the bridge, advancing the state
    // machine.  Return true (and populate `out`) the moment a complete,
    // valid frame is decoded.  The caller should loop until false.
    while (Serial1.available()) {
        uint8_t b = (uint8_t)Serial1.read();

        switch (rx_state) {
            case RxState::HUNT:
                if (b == CATAN_WIRE_MAGIC) {
                    rx_buf[0] = b;
                    rx_pos    = 1;
                    rx_state  = RxState::LEN;
                }
                break;

            case RxState::LEN:
                if (b == 0 || b > CATAN_MAX_PAYLOAD) {
                    // Invalid length — discard and hunt again
                    rx_state = RxState::HUNT;
                } else {
                    rx_buf[1]  = b;
                    rx_expect  = (uint8_t)(CATAN_FRAME_HEADER + b);
                    rx_pos     = CATAN_FRAME_HEADER;
                    rx_state   = RxState::BODY;
                }
                break;

            case RxState::BODY:
                rx_buf[rx_pos++] = b;
                if (rx_pos >= rx_expect) {
                    // Complete frame — attempt decode
                    rx_state = RxState::HUNT;
                    const uint8_t payload_len = rx_buf[1];
                    out = catan_PlayerInput_init_zero;
                    pb_istream_t is = pb_istream_from_buffer(
                        rx_buf + CATAN_FRAME_HEADER, payload_len);
                    if (!pb_decode(&is, catan_PlayerInput_fields, &out)) {
                        Serial.print(F("[COMM] decode PlayerInput failed: "));
                        Serial.println(PB_GET_ERROR(&is));
                        break;   // discard, keep draining
                    }
                    if (out.proto_version != CATAN_PROTO_VERSION) {
                        Serial.print(F("[COMM] proto version mismatch: "));
                        Serial.println(out.proto_version);
                        break;
                    }
                    return true;   // caller should decode more on next call
                }
                break;
        }
    }
    return false;
}

}  // namespace comm
