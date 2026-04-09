// =============================================================================
// sensor_manager.cpp — Hall effect sensor reading (direct GPIO + I2C expander).
// =============================================================================

#include "sensor_manager.h"
#include "board_layout.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>

namespace {

// Internal state arrays, indexed by ID.  Sized to the maximum ID range.
struct SensorState {
    bool current;
    bool previous;
    bool changed;
};

SensorState vertex_state[VERTEX_COUNT];
SensorState edge_state[EDGE_COUNT];
SensorState tile_state[TILE_COUNT];

// Cache of the last byte read from each I2C expander.
uint8_t expander_cache[EXPANDER_COUNT];

// Read a full byte from a PCF8574-style I2C expander.
uint8_t readExpander(uint8_t expander_idx) {
    if (expander_idx >= EXPANDER_COUNT) return 0xFF;
    Wire.requestFrom(EXPANDER_ADDRS[expander_idx], (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;  // All high = nothing detected (pull-up default)
}

bool readSensor(SensorSource source, uint8_t pin, uint8_t expander_idx) {
    if (source == SensorSource::DIRECT_GPIO) {
        return digitalRead(pin) == LOW;  // Active-low hall sensors
    }
    // I2C expander — read the bit from cache
    if (expander_idx >= EXPANDER_COUNT || pin >= PINS_PER_EXPANDER) return false;
    return !(expander_cache[expander_idx] & (1 << pin));  // Active-low
}

}  // anonymous namespace

namespace sensor {

void init() {
    memset(vertex_state, 0, sizeof(vertex_state));
    memset(edge_state, 0, sizeof(edge_state));
    memset(tile_state, 0, sizeof(tile_state));
    memset(expander_cache, 0xFF, sizeof(expander_cache));

    // Configure direct GPIO pins as INPUT_PULLUP
    for (uint8_t i = 0; i < g_vertex_sensor_count; ++i) {
        if (g_vertex_sensors[i].source == SensorSource::DIRECT_GPIO) {
            pinMode(g_vertex_sensors[i].pin, INPUT_PULLUP);
        }
    }
    for (uint8_t i = 0; i < g_edge_sensor_count; ++i) {
        if (g_edge_sensors[i].source == SensorSource::DIRECT_GPIO) {
            pinMode(g_edge_sensors[i].pin, INPUT_PULLUP);
        }
    }
    for (uint8_t i = 0; i < g_tile_sensor_count; ++i) {
        if (g_tile_sensors[i].source == SensorSource::DIRECT_GPIO) {
            pinMode(g_tile_sensors[i].pin, INPUT_PULLUP);
        }
    }

    // Initial read of all sensors
    poll();
    // Clear change flags after first read so nothing looks "changed" on boot.
    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) vertex_state[i].changed = false;
    for (uint8_t i = 0; i < EDGE_COUNT; ++i)   edge_state[i].changed = false;
    for (uint8_t i = 0; i < TILE_COUNT; ++i)    tile_state[i].changed = false;
}

void poll() {
    // Refresh I2C expander caches
    for (uint8_t e = 0; e < EXPANDER_COUNT; ++e) {
        expander_cache[e] = readExpander(e);
    }

    // Vertex sensors
    for (uint8_t i = 0; i < g_vertex_sensor_count; ++i) {
        const VertexSensor& vs = g_vertex_sensors[i];
        uint8_t id = vs.vertex_id;
        vertex_state[id].previous = vertex_state[id].current;
        vertex_state[id].current  = readSensor(vs.source, vs.pin, vs.expander_idx);
        vertex_state[id].changed  = (vertex_state[id].current != vertex_state[id].previous);
    }

    // Edge sensors
    for (uint8_t i = 0; i < g_edge_sensor_count; ++i) {
        const EdgeSensor& es = g_edge_sensors[i];
        uint8_t id = es.edge_id;
        edge_state[id].previous = edge_state[id].current;
        edge_state[id].current  = readSensor(es.source, es.pin, es.expander_idx);
        edge_state[id].changed  = (edge_state[id].current != edge_state[id].previous);
    }

    // Tile sensors (robber)
    for (uint8_t i = 0; i < g_tile_sensor_count; ++i) {
        const TileSensor& ts = g_tile_sensors[i];
        uint8_t id = ts.tile_id;
        tile_state[id].previous = tile_state[id].current;
        tile_state[id].current  = readSensor(ts.source, ts.pin, ts.expander_idx);
        tile_state[id].changed  = (tile_state[id].current != tile_state[id].previous);
    }
}

// ── Vertex ──────────────────────────────────────────────────────────────────

bool vertexPresent(uint8_t vertex_id) {
    return (vertex_id < VERTEX_COUNT) ? vertex_state[vertex_id].current : false;
}

bool vertexChanged(uint8_t vertex_id) {
    return (vertex_id < VERTEX_COUNT) ? vertex_state[vertex_id].changed : false;
}

// ── Edge ────────────────────────────────────────────────────────────────────

bool edgePresent(uint8_t edge_id) {
    return (edge_id < EDGE_COUNT) ? edge_state[edge_id].current : false;
}

bool edgeChanged(uint8_t edge_id) {
    return (edge_id < EDGE_COUNT) ? edge_state[edge_id].changed : false;
}

// ── Tile ────────────────────────────────────────────────────────────────────

bool tilePresent(uint8_t tile_id) {
    return (tile_id < TILE_COUNT) ? tile_state[tile_id].current : false;
}

bool tileChanged(uint8_t tile_id) {
    return (tile_id < TILE_COUNT) ? tile_state[tile_id].changed : false;
}

}  // namespace sensor
