// =============================================================================
// sensor_manager.cpp — Hall effect sensor reading on the ESP32-C6.
//
// All sensors live behind PCF8575 expanders on the I²C bus (SDA=GPIO6,
// SCL=GPIO7, 400 kHz). Each poll() reads every present expander once and
// updates per-element debounced state. Direct-GPIO sensors are still
// supported by the descriptor schema but the C6 board uses I²C exclusively.
// =============================================================================

#include "sensor_manager.h"
#include "pin_map.h"
#include "config.h"
#include "catan_log.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

namespace {

struct SensorState {
    bool current;
    bool previous;
    bool changed;
};

SensorState vertex_state[VERTEX_COUNT];
SensorState edge_state[EDGE_COUNT];
SensorState tile_state[TILE_COUNT];

uint16_t expander_cache[EXPANDER_COUNT];
bool     expander_present[EXPANDER_COUNT];

uint16_t readExpander(uint8_t idx) {
    if (idx >= EXPANDER_COUNT || !expander_present[idx]) return 0xFFFF;
    Wire.requestFrom(EXPANDER_ADDRS[idx], (uint8_t)2);
    if (Wire.available() >= 2) {
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        return (uint16_t)lo | ((uint16_t)hi << 8);
    }
    return 0xFFFF;  // all-high = nothing detected (pull-up default)
}

bool readSensor(const SensorPin& sp) {
    if (sp.pin == PIN_NONE) return false;
    if (sp.source == SensorSource::DIRECT_GPIO) {
        return digitalRead(sp.pin) == LOW;  // active-low Hall sensors
    }
    if (sp.expander_idx >= EXPANDER_COUNT || sp.pin >= PINS_PER_EXPANDER) return false;
    return !(expander_cache[sp.expander_idx] & (1u << sp.pin));
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

}  // namespace

namespace sensor {

void init() {
    memset(vertex_state, 0, sizeof(vertex_state));
    memset(edge_state, 0, sizeof(edge_state));
    memset(tile_state, 0, sizeof(tile_state));
    for (uint8_t e = 0; e < EXPANDER_COUNT; ++e) expander_cache[e] = 0xFFFF;

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_BUS_HZ);

    // Probe expanders. Missing ones are skipped on every subsequent poll.
    uint8_t found = 0;
    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        Wire.beginTransmission(EXPANDER_ADDRS[i]);
        uint8_t rc = Wire.endTransmission();
        expander_present[i] = (rc == 0);
        LOGI("I2C", "  expander 0x%02X %s", (unsigned)EXPANDER_ADDRS[i],
             expander_present[i] ? "OK" : "missing");
        if (expander_present[i]) {
            ++found;
            // Quasi-bidirectional input mode: write all-1s → enables weak
            // pull-ups so external Hall sensors can pull individual bits low.
            Wire.beginTransmission(EXPANDER_ADDRS[i]);
            Wire.write(0xFF);
            Wire.write(0xFF);
            Wire.endTransmission();
        }
    }
    LOGI("I2C", "probe: %u/%u expanders present", (unsigned)found, (unsigned)EXPANDER_COUNT);

    initPins(VERTEX_SENSOR_MAP, VERTEX_COUNT);
    initPins(EDGE_SENSOR_MAP,   EDGE_COUNT);
    initPins(TILE_SENSOR_MAP,   TILE_COUNT);

    poll();
    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) vertex_state[i].changed = false;
    for (uint8_t i = 0; i < EDGE_COUNT; ++i)   edge_state[i].changed   = false;
    for (uint8_t i = 0; i < TILE_COUNT; ++i)   tile_state[i].changed   = false;
}

void poll() {
    for (uint8_t e = 0; e < EXPANDER_COUNT; ++e) expander_cache[e] = readExpander(e);
    pollArray(VERTEX_SENSOR_MAP, vertex_state, VERTEX_COUNT);
    pollArray(EDGE_SENSOR_MAP,   edge_state,   EDGE_COUNT);
    pollArray(TILE_SENSOR_MAP,   tile_state,   TILE_COUNT);
}

bool vertexPresent(uint8_t v) { return v < VERTEX_COUNT && vertex_state[v].current; }
bool vertexChanged(uint8_t v) { return v < VERTEX_COUNT && vertex_state[v].changed; }

bool edgePresent(uint8_t e) { return e < EDGE_COUNT && edge_state[e].current; }
bool edgeChanged(uint8_t e) { return e < EDGE_COUNT && edge_state[e].changed; }

bool tilePresent(uint8_t t) { return t < TILE_COUNT && tile_state[t].current; }
bool tileChanged(uint8_t t) { return t < TILE_COUNT && tile_state[t].changed; }

}  // namespace sensor
