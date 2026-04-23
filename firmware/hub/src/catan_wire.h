#pragma once
// =============================================================================
// catan_wire.h — Shared UART/BLE framing helpers and nanopb codecs.
//
// An identical copy lives in firmware/board/src/. Keep them in sync.
//
// UART frame (Mega ⇆ hub, both directions):
//
//   [ 0xCA magic ][ type : uint8 ][ len : uint8 ][ payload : len bytes ][ crc8 : uint8 ]
//
// CRC-8 (poly 0x07, init 0x00) covers [type, len, payload]. Receivers
// drop frames whose first byte is not CATAN_WIRE_MAGIC or whose CRC fails.
//
// BLE payloads are bare nanopb bytes (no header) — ATT carries length.
//
// Schema version: 6.
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

#define CATAN_WIRE_MAGIC      0xCAu
#define CATAN_PROTO_VERSION   6u

// Message type codes used in the UART frame header.
#define CATAN_MSG_BOARD_STATE     0x01u
#define CATAN_MSG_PLAYER_INPUT    0x02u
#define CATAN_MSG_PLAYER_PRESENCE 0x03u

// Payload + framing sizes. Picked to comfortably exceed the largest
// nanopb-encoded message we currently emit; bump if you add fat fields.
#define CATAN_MAX_PAYLOAD     240u
#define CATAN_FRAME_OVERHEAD  4u        // magic + type + len + crc
#define CATAN_MAX_FRAME       (CATAN_MAX_PAYLOAD + CATAN_FRAME_OVERHEAD)

// Maximum number of player slots — must match firmware/board/src/config.h.
#define CATAN_MAX_PLAYERS     4u

// ---------------------------------------------------------------------------
// CRC-8 (poly 0x07, init 0x00). Tiny, table-free; called on short buffers.
// ---------------------------------------------------------------------------
static inline uint8_t catan_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Frame build / parse
// ---------------------------------------------------------------------------

// Wrap a nanopb payload in the UART envelope. Returns total frame length
// or 0 on failure (buffer too small / payload oversize).
static inline size_t catan_wire_pack(uint8_t type,
                                     const uint8_t* payload, size_t payload_len,
                                     uint8_t* frame, size_t cap) {
    if (payload_len > CATAN_MAX_PAYLOAD) return 0;
    if (cap < payload_len + CATAN_FRAME_OVERHEAD) return 0;
    frame[0] = CATAN_WIRE_MAGIC;
    frame[1] = type;
    frame[2] = (uint8_t)payload_len;
    if (payload_len) memcpy(frame + 3, payload, payload_len);
    frame[3 + payload_len] = catan_crc8(frame + 1, payload_len + 2);
    return payload_len + CATAN_FRAME_OVERHEAD;
}

// Validate a candidate frame buffer. On success returns true and fills
// out_type / out_payload / out_payload_len; otherwise returns false.
static inline bool catan_wire_unpack(const uint8_t* frame, size_t frame_len,
                                     uint8_t* out_type,
                                     const uint8_t** out_payload,
                                     uint8_t* out_payload_len) {
    if (frame_len < CATAN_FRAME_OVERHEAD) return false;
    if (frame[0] != CATAN_WIRE_MAGIC) return false;
    uint8_t len = frame[2];
    if ((size_t)len + CATAN_FRAME_OVERHEAD != frame_len) return false;
    uint8_t want_crc = catan_crc8(frame + 1, (size_t)len + 2);
    if (frame[3 + len] != want_crc) return false;
    *out_type = frame[1];
    *out_payload = frame + 3;
    *out_payload_len = len;
    return true;
}

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

static inline size_t catan_encode_player_presence(const catan_PlayerPresence* p,
                                                  uint8_t* buf, size_t cap) {
    pb_ostream_t os = pb_ostream_from_buffer(buf, cap);
    if (!pb_encode(&os, catan_PlayerPresence_fields, p)) return 0;
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

static inline bool catan_decode_player_presence(const uint8_t* buf, size_t len,
                                                catan_PlayerPresence* out) {
    memset(out, 0, sizeof(*out));
    pb_istream_t is = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&is, catan_PlayerPresence_fields, out)) return false;
    return out->proto_version == CATAN_PROTO_VERSION;
}

#ifdef __cplusplus
} // extern "C"
#endif
