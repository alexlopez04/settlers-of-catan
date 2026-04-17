// =============================================================================
// dice.cpp — Random dice utility.
// =============================================================================

#include "dice.h"
#include <Arduino.h>

namespace dice {

void init(uint16_t seed) {
    randomSeed(seed);
}

uint8_t roll(uint8_t& out_die1, uint8_t& out_die2) {
    out_die1 = (uint8_t)random(1, 7);
    out_die2 = (uint8_t)random(1, 7);
    return out_die1 + out_die2;
}

}  // namespace dice
