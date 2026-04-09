// =============================================================================
// button_manager.cpp — Debounced button input.
// =============================================================================

#include "button_manager.h"
#include "config.h"
#include <Arduino.h>

namespace {

struct ButtonState {
    uint8_t  pin;
    bool     last_stable;
    bool     last_raw;
    bool     pressed_event;   // One-shot flag, cleared after query
    uint32_t last_change_ms;
};

ButtonState buttons[3];

void initButton(ButtonState& b, uint8_t pin) {
    b.pin            = pin;
    b.last_stable    = true;   // Pull-up → HIGH when not pressed
    b.last_raw       = true;
    b.pressed_event  = false;
    b.last_change_ms = 0;
    pinMode(pin, INPUT_PULLUP);
}

void pollButton(ButtonState& b) {
    bool raw = digitalRead(b.pin);
    if (raw != b.last_raw) {
        b.last_change_ms = millis();
        b.last_raw = raw;
    }
    if ((millis() - b.last_change_ms) >= DEBOUNCE_MS) {
        if (raw != b.last_stable) {
            b.last_stable = raw;
            // Detect press (HIGH → LOW transition, active-low)
            if (!raw) {
                b.pressed_event = true;
            }
        }
    }
}

bool consumeEvent(ButtonState& b) {
    if (b.pressed_event) {
        b.pressed_event = false;
        return true;
    }
    return false;
}

}  // anonymous namespace

namespace button {

void init() {
    initButton(buttons[0], BTN_LEFT_PIN);    // Left
    initButton(buttons[1], BTN_CENTER_PIN);  // Center
    initButton(buttons[2], BTN_RIGHT_PIN);   // Right
}

void poll() {
    for (uint8_t i = 0; i < 3; ++i) {
        pollButton(buttons[i]);
    }
}

bool leftPressed()   { return consumeEvent(buttons[0]); }
bool centerPressed() { return consumeEvent(buttons[1]); }
bool rightPressed()  { return consumeEvent(buttons[2]); }

}  // namespace button
