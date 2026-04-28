#pragma once
// =============================================================================
// core/rng.h — Tiny RNG facade.
//
// Lets pure game logic call a single API for randomness whether it's running
// on Arduino (AVR, linked against `random()`) or on the native simulation
// host (linked against `rand()` from <cstdlib>). Implementation chosen at
// compile time in rng.cpp via `#ifdef ARDUINO`.
// =============================================================================

#include <stdint.h>

namespace core { namespace rng {

void     seed(uint32_t s);
// Uniform integer in [0, n); returns 0 if n == 0.
uint32_t uniform(uint32_t n);

}}  // namespace core::rng
