// =============================================================================
// sensor_manager.cpp — CV-backed piece detection.
//
// Replaces the I2C / direct-GPIO hall-effect implementation.  Piece
// placement data now arrives from the Raspberry Pi via cv_link (Serial2).
// The public API (vertexPresent / vertexChanged / edgePresent / edgeChanged)
// is unchanged so main.cpp and pumpSensors() need no modifications.
//
// Hysteresis: a piece that was present must appear absent for
// ABSENT_DEBOUNCE_FRAMES consecutive CV frames before we report a removal.
// This prevents false removals from momentary detection glitches.
// A piece appearing present is reported on the first CV frame that sees it.
//
// Tile sensors (robber detection) are not yet implemented via CV — they
// always return false.  The robber phase can still be triggered via the
// mobile app (ACTION_SKIP_ROBBER / tile selection).
// =============================================================================

#include "sensor_manager.h"
#include "cv_link.h"
#include "config.h"
#include "catan_log.h"

#include <string.h>

// Consecutive "absent" CV frames required before a piece is considered removed.
static constexpr uint8_t ABSENT_DEBOUNCE_FRAMES = 3;

namespace {

struct SensorState {
    bool    current;       // current debounced state
    bool    previous;      // state at start of last poll()
    bool    changed;       // current != previous (valid until next poll())
    uint8_t absent_count;  // # consecutive CV frames that saw this as empty
};

SensorState vertex_state[VERTEX_COUNT];
SensorState edge_state[EDGE_COUNT];

// Apply a new CV observation to one sensor slot.
static void updateState(SensorState& s, bool cv_present) {
    s.previous = s.current;
    if (cv_present) {
        s.absent_count = 0;
        s.current      = true;
    } else {
        // Only transition to absent after consistent absence across several frames.
        if (s.current) {
            if (++s.absent_count >= ABSENT_DEBOUNCE_FRAMES) {
                s.current      = false;
                s.absent_count = 0;
            }
        }
    }
    s.changed = (s.current != s.previous);
}

}  // anonymous namespace

namespace sensor {

void init() {
    memset(vertex_state, 0, sizeof(vertex_state));
    memset(edge_state,   0, sizeof(edge_state));
    cv_link::init();
}

void poll() {
    cv_link::poll();

    // Always reset changed flags so a stale "changed" from the previous frame
    // doesn't linger across multiple loop() iterations without a new CV frame.
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) vertex_state[v].changed = false;
    for (uint8_t e = 0; e < EDGE_COUNT;   ++e)   edge_state[e].changed = false;

    if (!cv_link::hasNewFrame()) return;

    uint8_t new_v[VERTEX_COUNT];
    uint8_t new_e[EDGE_COUNT];
    cv_link::getLatest(new_v, new_e);  // clears hasNewFrame()

    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        updateState(vertex_state[v], new_v[v] != 0xFF);
    }
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        updateState(edge_state[e], new_e[e] != 0xFF);
    }
}

bool vertexPresent(uint8_t v) { return (v < VERTEX_COUNT) && vertex_state[v].current; }
bool vertexChanged(uint8_t v) { return (v < VERTEX_COUNT) && vertex_state[v].changed; }

bool edgePresent(uint8_t e) { return (e < EDGE_COUNT) && edge_state[e].current; }
bool edgeChanged(uint8_t e) { return (e < EDGE_COUNT) && edge_state[e].changed; }

// Tile sensors are not implemented via CV yet — robber detection is handled
// manually by players using the mobile app.
bool tilePresent(uint8_t) { return false; }
bool tileChanged(uint8_t) { return false; }

}  // namespace sensor
