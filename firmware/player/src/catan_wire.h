#pragma once
// =============================================================================
// catan_wire.h — shared wire-frame framing used on every Catan link.
//
// NOTE: filename is catan_wire.h (NOT wire.h) to avoid clashing with the
// Arduino Wire.h header on case-insensitive filesystems (macOS).
//
// Frame format (all hops — I2C, LoRa, BLE):
//
//   [ 0xCA magic ] [ len : uint8 ] [ nanopb payload : len bytes ]
//
// The receiver MUST drop frames whose first byte is not 0xCA.
// =============================================================================

#include <stdint.h>
#include <stddef.h>

#define CATAN_WIRE_MAGIC       0xCA
#define CATAN_FRAME_HEADER     2      // magic + length byte
#define CATAN_MAX_PAYLOAD      200    // fits comfortably below LoRa/I2C limits
#define CATAN_MAX_FRAME        (CATAN_FRAME_HEADER + CATAN_MAX_PAYLOAD)

// Bump whenever BoardState / PlayerInput field semantics change.
#define CATAN_PROTO_VERSION    3u
