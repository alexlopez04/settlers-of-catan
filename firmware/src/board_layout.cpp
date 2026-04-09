// =============================================================================
// board_layout.cpp — Catan board topology data and randomization.
//
// *** WIRING GUIDE ***
// Edit the tables below to match your physical board.  Each tile, port,
// vertex sensor, edge sensor, and tile sensor has its own entry that maps
// a logical game element to physical hardware (LED indices, GPIO pins, or
// I2C expander bits).
// =============================================================================

#include "board_layout.h"
#include <Arduino.h>
#include <string.h>

// ── Human-readable names ────────────────────────────────────────────────────

const char* biomeName(Biome b) {
    switch (b) {
        case Biome::FOREST:   return "Forest";
        case Biome::PASTURE:  return "Pasture";
        case Biome::FIELD:    return "Field";
        case Biome::HILL:     return "Hill";
        case Biome::MOUNTAIN: return "Mountain";
        case Biome::DESERT:   return "Desert";
        default:              return "?";
    }
}

const char* portName(PortType p) {
    switch (p) {
        case PortType::GENERIC_3_1: return "3:1";
        case PortType::LUMBER_2_1:  return "Lumber 2:1";
        case PortType::WOOL_2_1:    return "Wool 2:1";
        case PortType::GRAIN_2_1:   return "Grain 2:1";
        case PortType::BRICK_2_1:   return "Brick 2:1";
        case PortType::ORE_2_1:     return "Ore 2:1";
        case PortType::NONE:        return "None";
        default:                    return "?";
    }
}

// ── Biome color (used by led_manager) ───────────────────────────────────────
// Defined here so board_layout owns the biome→color mapping too.

// ── Tile Table ──────────────────────────────────────────────────────────────
// Biome and number are filled in by randomizeBoardLayout().
// LED indices and sensor pins must match your wiring.
//
// led_index_a/b : indices into the WS2812B strip (0-based).
// sensor_pin    : GPIO pin (direct) or bit index (expander).
// vertices[6]   : the 6 vertex IDs surrounding this tile (clockwise from top).
// edges[6]      : the 6 edge IDs surrounding this tile (clockwise from top).
//
// Use 0xFF for unused vertex/edge slots on edge tiles that have fewer than 6.

Tile g_tiles[TILE_COUNT] = {
    // id  biome(placeholder)   num  ledA ledB  sPin  exp?   vertices (CW from top)         edges (CW from top)
    /*  0 */ { Biome::DESERT, 0,   0,  1,   22, false, { 0,  1,  2,  3,  4,  5}, { 0,  1,  2,  3,  4,  5} },
    /*  1 */ { Biome::DESERT, 0,   2,  3,   23, false, { 1,  6,  7,  2,  0,  5}, { 6,  7,  8,  1,  0,  5} }, // placeholder — edit
    /*  2 */ { Biome::DESERT, 0,   4,  5,   24, false, { 2,  7,  8,  9,  3, 10}, { 9, 10, 11, 12,  2,  1} },
    /*  3 */ { Biome::DESERT, 0,   6,  7,   25, false, { 5,  0,  3, 10, 11, 12}, { 5,  3, 12, 13, 14, 15} },
    /*  4 */ { Biome::DESERT, 0,   8,  9,   26, false, { 6, 13, 14,  7,  1,  0}, {16, 17, 18,  7,  6,  0} },
    /*  5 */ { Biome::DESERT, 0,  10, 11,   27, false, { 7, 14, 15, 16,  8,  2}, {18, 19, 20, 21, 10,  8} },
    /*  6 */ { Biome::DESERT, 0,  12, 13,   28, false, { 9,  8, 16, 17, 18,  3}, {11, 21, 22, 23, 24, 12} },
    /*  7 */ { Biome::DESERT, 0,  14, 15,   29, false, {10,  3, 18, 19, 20, 11}, {24, 25, 26, 27, 14, 13} },
    /*  8 */ { Biome::DESERT, 0,  16, 17,   30, false, {12, 11, 20, 21, 22, 23}, {15, 27, 28, 29, 30, 31} },
    /*  9 */ { Biome::DESERT, 0,  18, 19,   31, false, {13, 24, 25, 14,  6,  1}, {32, 33, 34, 17, 16,  6} },
    /* 10 */ { Biome::DESERT, 0,  20, 21,   32, false, {14, 25, 26, 27, 15,  7}, {34, 35, 36, 37, 19, 18} },
    /* 11 */ { Biome::DESERT, 0,  22, 23,   33, false, {16, 15, 27, 28, 29, 17}, {20, 37, 38, 39, 40, 22} },
    /* 12 */ { Biome::DESERT, 0,  24, 25,   34, false, {18, 17, 29, 30, 31, 19}, {23, 40, 41, 42, 43, 25} },
    /* 13 */ { Biome::DESERT, 0,  26, 27,   35, false, {20, 19, 31, 32, 33, 21}, {26, 43, 44, 45, 46, 28} },
    /* 14 */ { Biome::DESERT, 0,  28, 29,   36, false, {22, 21, 33, 34, 35, 23}, {29, 46, 47, 48, 49, 30} },
    /* 15 */ { Biome::DESERT, 0,  30, 31,   37, false, {25, 36, 37, 26, 14, 13}, {50, 51, 52, 35, 34, 33} },
    /* 16 */ { Biome::DESERT, 0,  32, 33,   38, false, {27, 26, 37, 38, 39, 28}, {36, 52, 53, 54, 55, 38} },
    /* 17 */ { Biome::DESERT, 0,  34, 35,   39, false, {29, 28, 39, 40, 41, 30}, {39, 55, 56, 57, 58, 41} },
    /* 18 */ { Biome::DESERT, 0,  36, 37,   40, false, {33, 32, 42, 43, 44, 34}, {45, 59, 60, 61, 62, 47} },
};

// ── Port Table ──────────────────────────────────────────────────────────────
// LED index for each port LED; vertices are the two coastal vertices.

Port g_ports[PORT_COUNT] = {
    // type                  led   vertices
    { PortType::GENERIC_3_1, 38,  { 0,  1} },
    { PortType::LUMBER_2_1,  39,  { 6, 13} },
    { PortType::GENERIC_3_1, 40,  {24, 36} },
    { PortType::WOOL_2_1,    41,  {37, 38} },
    { PortType::GENERIC_3_1, 42,  {40, 41} },
    { PortType::GRAIN_2_1,   43,  {43, 44} },
    { PortType::BRICK_2_1,   44,  {34, 35} },
    { PortType::GENERIC_3_1, 45,  {22, 23} },
    { PortType::ORE_2_1,     46,  {11, 12} },
};

// ── Vertex Sensors ──────────────────────────────────────────────────────────
// Map each vertex with a physical sensor to its hardware source.
// Vertices without sensors are simply omitted (no detection).
//
// For direct GPIO:  { vertex_id, SensorSource::DIRECT_GPIO, pin, 0 }
// For I2C expander: { vertex_id, SensorSource::I2C_EXPANDER, bit, expander_idx }

VertexSensor g_vertex_sensors[] = {
    // ── Direct GPIO examples (Mega pins 22-53) ──
    {  0, SensorSource::DIRECT_GPIO, 41, 0 },
    {  1, SensorSource::DIRECT_GPIO, 42, 0 },
    {  2, SensorSource::DIRECT_GPIO, 43, 0 },
    {  3, SensorSource::DIRECT_GPIO, 44, 0 },
    {  4, SensorSource::DIRECT_GPIO, 45, 0 },
    {  5, SensorSource::DIRECT_GPIO, 46, 0 },
    // ── I2C expander examples ──
    {  6, SensorSource::I2C_EXPANDER, 0, 0 },  // Expander 0x20, bit 0
    {  7, SensorSource::I2C_EXPANDER, 1, 0 },  // Expander 0x20, bit 1
    {  8, SensorSource::I2C_EXPANDER, 2, 0 },
    {  9, SensorSource::I2C_EXPANDER, 3, 0 },
    { 10, SensorSource::I2C_EXPANDER, 4, 0 },
    { 11, SensorSource::I2C_EXPANDER, 5, 0 },
    { 12, SensorSource::I2C_EXPANDER, 6, 0 },
    { 13, SensorSource::I2C_EXPANDER, 7, 0 },
    { 14, SensorSource::I2C_EXPANDER, 0, 1 },  // Expander 0x21, bit 0
    { 15, SensorSource::I2C_EXPANDER, 1, 1 },
    { 16, SensorSource::I2C_EXPANDER, 2, 1 },
    { 17, SensorSource::I2C_EXPANDER, 3, 1 },
    { 18, SensorSource::I2C_EXPANDER, 4, 1 },
    { 19, SensorSource::I2C_EXPANDER, 5, 1 },
    { 20, SensorSource::I2C_EXPANDER, 6, 1 },
    { 21, SensorSource::I2C_EXPANDER, 7, 1 },
    // ... add more as needed for your board
};

const uint8_t g_vertex_sensor_count = sizeof(g_vertex_sensors) / sizeof(g_vertex_sensors[0]);

// ── Edge Sensors ────────────────────────────────────────────────────────────

EdgeSensor g_edge_sensors[] = {
    {  0, SensorSource::I2C_EXPANDER, 0, 2 },  // Expander 0x22, bit 0
    {  1, SensorSource::I2C_EXPANDER, 1, 2 },
    {  2, SensorSource::I2C_EXPANDER, 2, 2 },
    {  3, SensorSource::I2C_EXPANDER, 3, 2 },
    {  4, SensorSource::I2C_EXPANDER, 4, 2 },
    {  5, SensorSource::I2C_EXPANDER, 5, 2 },
    {  6, SensorSource::I2C_EXPANDER, 6, 2 },
    {  7, SensorSource::I2C_EXPANDER, 7, 2 },
    // ... add more as needed
};

const uint8_t g_edge_sensor_count = sizeof(g_edge_sensors) / sizeof(g_edge_sensors[0]);

// ── Tile Sensors (robber detection) ─────────────────────────────────────────

TileSensor g_tile_sensors[] = {
    {  0, SensorSource::I2C_EXPANDER, 0, 3 },  // Expander 0x23, bit 0
    {  1, SensorSource::I2C_EXPANDER, 1, 3 },
    {  2, SensorSource::I2C_EXPANDER, 2, 3 },
    // ... add more as needed
};

const uint8_t g_tile_sensor_count = sizeof(g_tile_sensors) / sizeof(g_tile_sensors[0]);

// ── Adjacency ───────────────────────────────────────────────────────────────

uint8_t tilesForVertex(uint8_t vertex_id, uint8_t out[], uint8_t max_out) {
    uint8_t count = 0;
    for (uint8_t t = 0; t < TILE_COUNT && count < max_out; ++t) {
        for (uint8_t v = 0; v < 6; ++v) {
            if (g_tiles[t].vertices[v] == vertex_id) {
                out[count++] = t;
                break;
            }
        }
    }
    return count;
}

uint8_t tilesForEdge(uint8_t edge_id, uint8_t out[], uint8_t max_out) {
    uint8_t count = 0;
    for (uint8_t t = 0; t < TILE_COUNT && count < max_out; ++t) {
        for (uint8_t e = 0; e < 6; ++e) {
            if (g_tiles[t].edges[e] == edge_id) {
                out[count++] = t;
                break;
            }
        }
    }
    return count;
}

// ── Randomization ───────────────────────────────────────────────────────────

static void shuffleArray(uint8_t* arr, uint8_t n) {
    for (uint8_t i = n - 1; i > 0; --i) {
        uint8_t j = random(0, i + 1);
        uint8_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

void randomizeBoardLayout() {
    // Standard Catan biome distribution:
    // 4 Forest, 4 Pasture, 4 Field, 3 Hill, 3 Mountain, 1 Desert = 19 tiles
    uint8_t biomes[TILE_COUNT];
    uint8_t idx = 0;
    for (uint8_t i = 0; i < 4; ++i) biomes[idx++] = (uint8_t)Biome::FOREST;
    for (uint8_t i = 0; i < 4; ++i) biomes[idx++] = (uint8_t)Biome::PASTURE;
    for (uint8_t i = 0; i < 4; ++i) biomes[idx++] = (uint8_t)Biome::FIELD;
    for (uint8_t i = 0; i < 3; ++i) biomes[idx++] = (uint8_t)Biome::HILL;
    for (uint8_t i = 0; i < 3; ++i) biomes[idx++] = (uint8_t)Biome::MOUNTAIN;
    biomes[idx++] = (uint8_t)Biome::DESERT;

    shuffleArray(biomes, TILE_COUNT);

    // Standard Catan number tokens (18 tokens for 18 non-desert tiles):
    // 1x2, 2x3, 2x4, 2x5, 2x6, 2x8, 2x9, 2x10, 2x11, 1x12
    uint8_t numbers[] = { 2, 3, 3, 4, 4, 5, 5, 6, 6, 8, 8, 9, 9, 10, 10, 11, 11, 12 };
    shuffleArray(numbers, 18);

    uint8_t num_idx = 0;
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        g_tiles[t].biome = (Biome)biomes[t];
        if (g_tiles[t].biome == Biome::DESERT) {
            g_tiles[t].number = 0;  // Desert has no number
        } else {
            g_tiles[t].number = numbers[num_idx++];
        }
    }
}
