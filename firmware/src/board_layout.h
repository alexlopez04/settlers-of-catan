#pragma once
// =============================================================================
// board_layout.h — Catan board topology: tiles, ports, vertices, edges,
// sensor mappings, LED mappings, and adjacency tables.
//
// Edit the tables in board_layout.cpp to match your physical board wiring.
// =============================================================================

#include <stdint.h>
#include "config.h"

// ── Biome / Resource Types ──────────────────────────────────────────────────
enum class Biome : uint8_t {
    FOREST,    // Lumber
    PASTURE,   // Wool
    FIELD,     // Grain
    HILL,      // Brick
    MOUNTAIN,  // Ore
    DESERT,    // No resource
    NUM_BIOMES
};

const char* biomeName(Biome b);

// ── Port Types ──────────────────────────────────────────────────────────────
enum class PortType : uint8_t {
    GENERIC_3_1,   // 3:1 any resource
    LUMBER_2_1,
    WOOL_2_1,
    GRAIN_2_1,
    BRICK_2_1,
    ORE_2_1,
    NONE
};

const char* portName(PortType p);

// ── Tile ────────────────────────────────────────────────────────────────────
struct Tile {
    Biome    biome;
    uint8_t  number;           // 2–12 (0 for desert)
    uint8_t  led_index_a;      // First LED index in the strip
    uint8_t  led_index_b;      // Second LED index in the strip
    uint8_t  sensor_pin;       // Hall sensor pin (GPIO or expander-mapped)
    bool     sensor_via_expander; // true → pin refers to expander bit
    uint8_t  vertices[6];      // IDs of the 6 surrounding vertices
    uint8_t  edges[6];         // IDs of the 6 surrounding edges
};

// ── Port ────────────────────────────────────────────────────────────────────
struct Port {
    PortType type;
    uint8_t  led_index;        // LED index in the strip
    uint8_t  vertices[2];      // The two vertices this port touches
};

// ── Sensor Source ───────────────────────────────────────────────────────────
enum class SensorSource : uint8_t {
    DIRECT_GPIO,
    I2C_EXPANDER
};

struct VertexSensor {
    uint8_t       vertex_id;
    SensorSource  source;
    uint8_t       pin;          // GPIO pin or expander bit index
    uint8_t       expander_idx; // Index into EXPANDER_ADDRS (only if I2C_EXPANDER)
};

struct EdgeSensor {
    uint8_t       edge_id;
    SensorSource  source;
    uint8_t       pin;
    uint8_t       expander_idx;
};

struct TileSensor {
    uint8_t       tile_id;
    SensorSource  source;
    uint8_t       pin;
    uint8_t       expander_idx;
};

// ── Adjacency helpers ───────────────────────────────────────────────────────
// Returns the tile IDs that share `vertex_id`. `out` must hold up to 3 entries.
uint8_t tilesForVertex(uint8_t vertex_id, uint8_t out[], uint8_t max_out);

// Returns the tile IDs adjacent to `edge_id`. `out` must hold up to 2 entries.
uint8_t tilesForEdge(uint8_t edge_id, uint8_t out[], uint8_t max_out);

// ── Global layout tables (defined in board_layout.cpp) ──────────────────────
extern Tile          g_tiles[TILE_COUNT];
extern Port          g_ports[PORT_COUNT];
extern VertexSensor  g_vertex_sensors[];
extern EdgeSensor    g_edge_sensors[];
extern TileSensor    g_tile_sensors[];

extern const uint8_t g_vertex_sensor_count;
extern const uint8_t g_edge_sensor_count;
extern const uint8_t g_tile_sensor_count;

// ── Initialization ──────────────────────────────────────────────────────────
// Randomly assigns biomes and numbers according to standard Catan rules.
void randomizeBoardLayout();
