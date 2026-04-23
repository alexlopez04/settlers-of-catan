#pragma once
// =============================================================================
// catan_wire.h — shared wire framing + Envelope helpers for every Catan link.
//
// NOTE: filename is catan_wire.h (NOT wire.h) to avoid clashing with the
// Arduino Wire.h header on case-insensitive filesystems (macOS).
//
// Frame format (all hops — Serial, LoRa, BLE):
//
//   [ 0xCA magic ] [ len : uint8 ] [ nanopb Envelope payload : len bytes ]
//
// The receiver MUST drop frames whose first byte is not 0xCA.
//
// Every firmware links its own copy of this header alongside the generated
// `proto/catan.pb.{h,c}` so the helpers below can be `static inline`.
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

#define CATAN_WIRE_MAGIC       0xCA
#define CATAN_FRAME_HEADER     2      // magic + length byte
#define CATAN_MAX_PAYLOAD      200    // fits comfortably below LoRa/I2C limits
#define CATAN_MAX_FRAME        (CATAN_FRAME_HEADER + CATAN_MAX_PAYLOAD)

// Bump whenever Envelope / BoardState / PlayerInput field semantics change.
#define CATAN_PROTO_VERSION    4u

// ---------------------------------------------------------------------------
// Node identifiers (populate Envelope.sender_id).
//
// Each physical device has exactly one sender_id. The bridge is stateless and
// passes traffic through without rewriting sender_id, so the originator is
// always visible to the final receiver.
// ---------------------------------------------------------------------------
#define CATAN_NODE_UNKNOWN     0u
#define CATAN_NODE_BOARD       1u
#define CATAN_NODE_BRIDGE      2u
#define CATAN_NODE_PLAYER_BASE 10u    // player N -> 10 + N  (N in 0..3)
#define CATAN_NODE_MOBILE_BASE 20u    // mobile N -> 20 + N

static inline uint32_t catan_node_player(uint32_t n) { return CATAN_NODE_PLAYER_BASE + n; }
static inline uint32_t catan_node_mobile(uint32_t n) { return CATAN_NODE_MOBILE_BASE + n; }

static inline bool catan_node_is_player(uint32_t id) {
    return id >= CATAN_NODE_PLAYER_BASE && id < CATAN_NODE_PLAYER_BASE + 4u;
}
static inline bool catan_node_is_mobile(uint32_t id) {
    return id >= CATAN_NODE_MOBILE_BASE && id < CATAN_NODE_MOBILE_BASE + 4u;
}

// Convenience: player index (0..3) for a player/mobile sender_id, or 0xFF.
static inline uint8_t catan_node_player_index(uint32_t id) {
    if (catan_node_is_player(id)) return (uint8_t)(id - CATAN_NODE_PLAYER_BASE);
    if (catan_node_is_mobile(id)) return (uint8_t)(id - CATAN_NODE_MOBILE_BASE);
    return 0xFF;
}

// ---------------------------------------------------------------------------
// Envelope encode/decode into/from a wire frame buffer.
//
// catan_wire_encode():
//   Fills caller-provided `frame` with [magic][len][payload] for `env`.
//   Returns the full frame length on success, 0 on failure (encode error or
//   payload > CATAN_MAX_PAYLOAD). `frame_cap` must be >= CATAN_MAX_FRAME.
//
// catan_wire_decode():
//   Parses a single framed envelope from `frame` (must start with magic).
//   Returns true on success (`out_env` populated); false on bad framing,
//   truncation, or protobuf decode failure.
// ---------------------------------------------------------------------------

static inline size_t catan_wire_encode(const catan_Envelope *env,
                                       uint8_t *frame, size_t frame_cap) {
    if (frame_cap < CATAN_MAX_FRAME) return 0;
    pb_ostream_t os = pb_ostream_from_buffer(frame + CATAN_FRAME_HEADER,
                                             CATAN_MAX_PAYLOAD);
    if (!pb_encode(&os, catan_Envelope_fields, env)) return 0;
    if (os.bytes_written > CATAN_MAX_PAYLOAD) return 0;
    frame[0] = CATAN_WIRE_MAGIC;
    frame[1] = (uint8_t)os.bytes_written;
    return CATAN_FRAME_HEADER + os.bytes_written;
}

static inline bool catan_wire_decode(const uint8_t *frame, size_t frame_len,
                                     catan_Envelope *out_env) {
    if (frame_len < CATAN_FRAME_HEADER) return false;
    if (frame[0] != CATAN_WIRE_MAGIC) return false;
    uint8_t len = frame[1];
    if (len == 0 || len > CATAN_MAX_PAYLOAD) return false;
    if (frame_len < (size_t)CATAN_FRAME_HEADER + (size_t)len) return false;
    memset(out_env, 0, sizeof(*out_env));
    pb_istream_t is = pb_istream_from_buffer(frame + CATAN_FRAME_HEADER, len);
    return pb_decode(&is, catan_Envelope_fields, out_env);
}

// Decode just the payload portion (no magic/length) — useful when the
// transport has already framed the bytes for us (e.g. BLE GATT write).
static inline bool catan_wire_decode_payload(const uint8_t *payload, size_t len,
                                             catan_Envelope *out_env) {
    if (len == 0 || len > CATAN_MAX_PAYLOAD) return false;
    memset(out_env, 0, sizeof(*out_env));
    pb_istream_t is = pb_istream_from_buffer(payload, len);
    return pb_decode(&is, catan_Envelope_fields, out_env);
}

// Validate that an Envelope is self-consistent and uses our protocol version.
// Returns true if the envelope is well-formed and safe to dispatch.
static inline bool catan_wire_envelope_valid(const catan_Envelope *env) {
    if (env->proto_version != CATAN_PROTO_VERSION) return false;
    if (env->sender_id == CATAN_NODE_UNKNOWN) return false;
    // The oneof tag must match the declared message_type (if the sender set
    // one). Senders that leave message_type=MSG_UNSPECIFIED are tolerated.
    if (env->message_type != catan_MessageType_MSG_UNSPECIFIED &&
        (uint32_t)env->message_type != (uint32_t)env->which_body) {
        return false;
    }
    return true;
}

#ifdef __cplusplus
} // extern "C"
#endif
