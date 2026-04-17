#pragma once
// =============================================================================
// pin_map.h — Sensor-to-pin mapping for tiles, vertices, and edges.
//
// Each board element has one SensorPin entry.  Set `pin = PIN_NONE` for
// elements that have no physical sensor.
//
// Two source types:
//   DIRECT_GPIO  — sensor wired to an Arduino digital pin
//   I2C_EXPANDER — sensor on a PCF8574/MCP23017; specify expander index + bit
//
// Edit pin_map.cpp to match your physical wiring.
// =============================================================================

#include <stdint.h>
#include "config.h"
#include "board_types.h"

// ── Sensor pin descriptor ───────────────────────────────────────────────────
struct SensorPin {
    SensorSource source;       // DIRECT_GPIO or I2C_EXPANDER
    uint8_t      pin;          // GPIO pin number, or bit index on expander
                               // PIN_NONE (0xFF) = no sensor attached
    uint8_t      expander_idx; // Index into EXPANDER_ADDRS[] (ignored for GPIO)
};

// Convenience constructors for readability in the mapping tables:
//   GPIO(pin)                → direct Arduino digital pin
//   EXPANDER(bit, idx)       → I2C expander bit on expander #idx
//   NO_SENSOR                → no sensor connected
#define GPIO(pin)             { SensorSource::DIRECT_GPIO,  (pin),     0 }
#define EXPANDER(bit, idx)    { SensorSource::I2C_EXPANDER, (bit),   (idx) }
#define NO_SENSOR             { SensorSource::DIRECT_GPIO,  PIN_NONE,  0 }

// ── Mapping tables (defined in pin_map.cpp) ─────────────────────────────────

// One entry per tile — for robber / piece-on-hex detection.
extern const SensorPin TILE_SENSOR_MAP[TILE_COUNT];

// One entry per vertex — for settlement / city detection.
extern const SensorPin VERTEX_SENSOR_MAP[VERTEX_COUNT];

// One entry per edge — for road detection.
extern const SensorPin EDGE_SENSOR_MAP[EDGE_COUNT];
