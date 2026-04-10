#pragma once
// =============================================================================
// board_types.h — Shared enums, coordinate types, and constants for the
// Catan board.  No hardware-specific details here.
//
// Terminology (planar graph model):
//   Tile   (hex/face)   — the 19 resource hexagons  → axial coordinates (q, r)
//   Vertex (intersection) — where up to 3 tiles meet → settlement/city sites
//   Edge   (connection)   — between adjacent vertices → road sites
//   Port   (harbour)      — coastal trade locations
// =============================================================================

#include <stdint.h>

// ── Sentinel for "nothing" / unused slot ────────────────────────────────────
static constexpr uint8_t NONE = 0xFF;

// ── Axial hex coordinate ────────────────────────────────────────────────────
struct HexCoord {
    int8_t q;
    int8_t r;
};

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
    PORT_NONE
};

const char* portName(PortType p);

// ── Sensor source (how a sensor is physically wired) ────────────────────────
enum class SensorSource : uint8_t {
    DIRECT_GPIO,    // Connected to an Arduino digital pin
    I2C_EXPANDER    // Connected to a PCF8574 / MCP23017 I2C GPIO expander
};

// ── Pin sentinel — means "no sensor attached" ───────────────────────────────
static constexpr uint8_t PIN_NONE = 0xFF;
