#pragma once
// =============================================================================
// catan_wire.h — nanopb encode/decode helpers for the BLE wire format.
//
// The board MCU and the mobile app exchange bare nanopb payloads (no
// framing) over BLE GATT — ATT carries length, so no magic / CRC layer is
// needed. After the v8 merge there is no UART link to wrap.
//
// Schema version: 8.
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <pb.h>
#include <pb_encode.h>
#include <pb_decode.h>

#include "proto/catan.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CATAN_PROTO_VERSION   8u

// Buffer cap for the largest BLE payload. Worst-case BoardState is ~520
// bytes (nanopb computed) but real PLAYING-phase encodes are well under
// 350. 480 leaves headroom under the 509-byte ATT notify limit at MTU 512.
#define CATAN_MAX_PAYLOAD     480u

#define CATAN_MAX_PLAYERS     4u

// ---------------------------------------------------------------------------
// nanopb encode helpers — return payload length or 0 on failure.
// ---------------------------------------------------------------------------

static inline size_t catan_encode_board_state(const catan_BoardState* s,
                                              uint8_t* buf, size_t cap) {
    pb_ostream_t os = pb_ostream_from_buffer(buf, cap);
    if (!pb_encode(&os, catan_BoardState_fields, s)) return 0;
    return os.bytes_written;
}

static inline size_t catan_encode_player_input(const catan_PlayerInput* in,
                                               uint8_t* buf, size_t cap) {
    pb_ostream_t os = pb_ostream_from_buffer(buf, cap);
    if (!pb_encode(&os, catan_PlayerInput_fields, in)) return 0;
    return os.bytes_written;
}

// ---------------------------------------------------------------------------
// nanopb decode helpers — true on success AND matching proto version.
// ---------------------------------------------------------------------------

static inline bool catan_decode_board_state(const uint8_t* buf, size_t len,
                                            catan_BoardState* out) {
    memset(out, 0, sizeof(*out));
    pb_istream_t is = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&is, catan_BoardState_fields, out)) return false;
    return out->proto_version == CATAN_PROTO_VERSION;
}

static inline bool catan_decode_player_input(const uint8_t* buf, size_t len,
                                             catan_PlayerInput* out) {
    memset(out, 0, sizeof(*out));
    pb_istream_t is = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&is, catan_PlayerInput_fields, out)) return false;
    return out->proto_version == CATAN_PROTO_VERSION;
}

#ifdef __cplusplus
} // extern "C"
#endif
