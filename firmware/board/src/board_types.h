#pragma once
// =============================================================================
// board_types.h — Shared enums, coordinate types, and constants.
// =============================================================================

#include <stdint.h>

static constexpr uint8_t NONE = 0xFF;

struct HexCoord {
    int8_t q;
    int8_t r;
};

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

enum class PortType : uint8_t {
    GENERIC_3_1,
    LUMBER_2_1,
    WOOL_2_1,
    GRAIN_2_1,
    BRICK_2_1,
    ORE_2_1,
    PORT_NONE
};

const char* portName(PortType p);

// Maps Biome → resource index (0-4) for the 5 resource types.
// Returns NONE for DESERT.
inline uint8_t biomeToResource(Biome b) {
    switch (b) {
        case Biome::FOREST:   return 0;  // Lumber
        case Biome::PASTURE:  return 1;  // Wool
        case Biome::FIELD:    return 2;  // Grain
        case Biome::HILL:     return 3;  // Brick
        case Biome::MOUNTAIN: return 4;  // Ore
        default:              return NONE;
    }
}

// Board generation difficulty (set by Player 1 in the lobby).
enum class Difficulty : uint8_t {
    EASY   = 0,  // Balanced / Beginner Friendly — constraint-based rejection sampling
    NORMAL = 1,  // Classic Catan feel — near-standard random layout
    HARD   = 2,  // Competitive / Skill-Focused — biased generation with scoring
    EXPERT = 3,  // Punishing / Tournament Chaos — adversarial, maximally unfair
};

enum class SensorSource : uint8_t {
    DIRECT_GPIO,
    I2C_EXPANDER
};

static constexpr uint8_t PIN_NONE = 0xFF;
