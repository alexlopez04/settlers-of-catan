#pragma once
// =============================================================================
// sensor_manager.h — Hall effect sensor reading via direct GPIO and I2C
// GPIO expanders.
// =============================================================================

#include <stdint.h>

namespace sensor {

void init();

// Poll all sensors and update internal state. Call once per loop iteration.
void poll();

// ── Vertex sensors ──────────────────────────────────────────────────────────
bool vertexPresent(uint8_t vertex_id);
bool vertexChanged(uint8_t vertex_id);  // true if state changed since last poll

// ── Edge sensors ────────────────────────────────────────────────────────────
bool edgePresent(uint8_t edge_id);
bool edgeChanged(uint8_t edge_id);

// ── Tile sensors (robber detection) ─────────────────────────────────────────
bool tilePresent(uint8_t tile_id);
bool tileChanged(uint8_t tile_id);

}  // namespace sensor
