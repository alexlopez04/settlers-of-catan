#pragma once
// =============================================================================
// button_manager.h — Debounced button reading.
// =============================================================================

#include <stdint.h>

namespace button {

void init();

// Call once per loop iteration.
void poll();

// Returns true on the loop tick when a button transitions from released to
// pressed (rising edge after debounce).  Each button's meaning is
// phase-dependent — see main.cpp.
bool leftPressed();    // Pin BTN_LEFT_PIN   (left button)
bool centerPressed();  // Pin BTN_CENTER_PIN (center button)
bool rightPressed();   // Pin BTN_RIGHT_PIN  (right button)

}  // namespace button
