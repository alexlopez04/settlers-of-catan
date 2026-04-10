// =============================================================================
// sensor_manager.cpp — Hall effect sensor reading (direct GPIO + I2C expander).
//
// Uses the pin mapping tables from pin_map.h/cpp.  Sensors with
// pin == PIN_NONE are skipped (no hardware connected).
// =============================================================================

#include "sensor_manager.h"
#include "pin_map.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>

namespace {

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

uint8_t readExpander(uint8_t expander_idx) {
    if (expander_idx >= EXPANDER_COUNT) return 0xFF;
    Wire.requestFrom(EXPANDER_ADDRS[expander_idx], (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;  // All high = nothing detected (pull-up default)
}

bool readSensor(const SensorPin& sp) {
    if (sp.pin == PIN_NONE) return false;  // No sensor connected
    if (sp.source == SensorSource::DIRECT_GPIO) {
        return digitalRead(sp.pin) == LOW;  // Active-low hall sensors
    }
    // I2C expander — read the bit from cache
    if (sp.expander_idx >= EXPANDER_COUNT || sp.pin >= PINS_PER_EXPANDER) return false;
    return !(expander_cache[sp.expander_idx] & (1 << sp.pin));  // Active-low
}

void initPins(const SensorPin* map, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        if (map[i].pin != PIN_NONE && map[i].source == SensorSource::DIRECT_GPIO) {
            pinMode(map[i].pin, INPUT_PULLUP);
        }
    }
}

void pollArray(const SensorPin* map, SensorState* state, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        state[i].previous = state[i].current;
        state[i].current  = readSensor(map[i]);
        state[i].changed  = (state[i].current != state[i].previous);
    }
}

}  // anonymous namespace

namespace sensor {

void init() {
    memset(vertex_state, 0, sizeof(vertex_state));
    memset(edge_state, 0, sizeof(edge_state));
    memset(tile_state, 0, sizeof(tile_state));
    memset(expander_cache, 0xFF, sizeof(expander_cache));

    // Configure direct GPIO pins as INPUT_PULLUP
    initPins(VERTEX_SENSOR_MAP, VERTEX_COUNT);
    initPins(EDGE_SENSOR_MAP,   EDGE_COUNT);
    initPins(TILE_SENSOR_MAP,   TILE_COUNT);

    // Initial read
    poll();
    // Clear change flags so nothing looks "changed" on boot
    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) vertex_state[i].changed = false;
    for (uint8_t i = 0; i < EDGE_COUNT; ++i)   edge_state[i].changed = false;
    for (uint8_t i = 0; i < TILE_COUNT; ++i)    tile_state[i].changed = false;
}

void poll() {
    // Refresh I2C expander caches
    for (uint8_t e = 0; e < EXPANDER_COUNT; ++e) {
        expander_cache[e] = readExpander(e);
    }

    pollArray(VERTEX_SENSOR_MAP, vertex_state, VERTEX_COUNT);
    pollArray(EDGE_SENSOR_MAP,   edge_state,   EDGE_COUNT);
    pollArray(TILE_SENSOR_MAP,   tile_state,    TILE_COUNT);
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
