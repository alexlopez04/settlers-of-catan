// =============================================================================
// core/rng.cpp — Dual-target RNG. Arduino uses random(); native uses rand().
// =============================================================================

#include "core/rng.h"

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <cstdlib>
#endif

namespace core { namespace rng {

void seed(uint32_t s) {
#ifdef ARDUINO
    randomSeed((unsigned long)s);
#else
    std::srand((unsigned int)s);
#endif
}

uint32_t uniform(uint32_t n) {
    if (n == 0) return 0;
#ifdef ARDUINO
    return (uint32_t)random((long)n);
#else
    return (uint32_t)(std::rand() % (int)n);
#endif
}

}}  // namespace core::rng
