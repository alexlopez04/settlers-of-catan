#pragma once
// =============================================================================
// dice.h — Random dice roll utility.
// =============================================================================

#include <stdint.h>

namespace dice {

// Seed the RNG (call once in setup, e.g. from analogRead on a floating pin).
void init(uint16_t seed);

// Roll two six-sided dice. Returns total (2–12).
// Individual results stored in `out_die1` and `out_die2`.
uint8_t roll(uint8_t &out_die1, uint8_t &out_die2);

}  // namespace dice
