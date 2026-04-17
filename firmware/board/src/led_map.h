#pragma once
// =============================================================================
// led_map.h — LED strip address mapping for tiles and ports.
//
// Each tile or port can have a variable number of LEDs assigned to it
// (up to MAX_LEDS_PER_TILE / MAX_LEDS_PER_PORT).  Set `count` to the
// number of LEDs actually used, and fill the `indices[]` array with the
// corresponding positions in the WS2812B strip.
//
// Edit led_map.cpp to match your physical wiring.
// =============================================================================

#include <stdint.h>
#include "config.h"

// ── LED group for a single board element ────────────────────────────────────
struct TileLedMap {
    uint16_t indices[MAX_LEDS_PER_TILE];  // LED strip positions
    uint8_t  count;                       // How many LEDs are used (0 = none)
};

struct PortLedMap {
    uint16_t indices[MAX_LEDS_PER_PORT];  // LED strip positions
    uint8_t  count;                       // How many LEDs are used (0 = none)
};

// ── Mapping tables (defined in led_map.cpp) ─────────────────────────────────

// One entry per tile (T00–T18).  Default: 2 LEDs per tile.
extern const TileLedMap TILE_LED_MAP[TILE_COUNT];

// One entry per port (P0–P8).  Default: 1 LED per port.
extern const PortLedMap PORT_LED_MAP[PORT_COUNT];
