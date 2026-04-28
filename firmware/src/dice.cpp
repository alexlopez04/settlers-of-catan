// =============================================================================
// dice.cpp — Random dice utility, delegating to core::rng so the same code
// works on Arduino and on the native simulation host.
// =============================================================================

#include "dice.h"
#include "core/rng.h"

namespace dice {

void init(uint16_t seed) {
    core::rng::seed((uint32_t)seed);
}

uint8_t roll(uint8_t& out_die1, uint8_t& out_die2) {
    out_die1 = (uint8_t)(1u + core::rng::uniform(6));
    out_die2 = (uint8_t)(1u + core::rng::uniform(6));
    return (uint8_t)(out_die1 + out_die2);
}

}  // namespace dice
