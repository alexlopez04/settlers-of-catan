// =============================================================================
// input.cpp — Debounced button driver implementation.
// =============================================================================

#include "input.h"
#include "config.h"

#include <Arduino.h>

namespace input {

// ── Private state ─────────────────────────────────────────────────────────────

namespace {

struct BtnState {
    int           pin;
    bool          raw_prev;      // last sampled GPIO level (active-LOW)
    bool          debounced;     // stable debounced state (true = pressed)
    uint32_t      last_edge_ms;  // timestamp of last raw edge
    bool          long_fired;    // long-press event already delivered this hold
};

static constexpr int PIN_TABLE[3] = { BTN_A_PIN, BTN_B_PIN, BTN_C_PIN };

static BtnState  g_btns[3];
static EventHandler g_handler = nullptr;

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void init(EventHandler handler) {
    g_handler = handler;
    for (uint8_t i = 0; i < 3; ++i) {
        g_btns[i] = {
            .pin         = PIN_TABLE[i],
            .raw_prev    = false,
            .debounced   = false,
            .last_edge_ms = 0,
            .long_fired  = false,
        };
        pinMode(PIN_TABLE[i], INPUT_PULLUP);
    }
}

void tick() {
    if (!g_handler) return;

    const uint32_t now = millis();

    for (uint8_t i = 0; i < 3; ++i) {
        BtnState& b = g_btns[i];

        // Active-LOW: pressed == LOW == true in our logic
        const bool raw = (digitalRead(b.pin) == LOW);

        if (raw != b.raw_prev) {
            b.raw_prev    = raw;
            b.last_edge_ms = now;
        }

        // Has the signal been stable for the debounce window?
        const uint32_t stable_ms = now - b.last_edge_ms;
        if (stable_ms < BTN_DEBOUNCE_MS) continue;

        const bool was_pressed = b.debounced;
        b.debounced = raw;

        if (!was_pressed && raw) {
            // Falling edge (press): reset long-press flag, wait for release or hold.
            b.long_fired = false;
        } else if (was_pressed && !raw) {
            // Rising edge (release): if long-press hasn't fired, it's a short press.
            if (!b.long_fired) {
                g_handler({ static_cast<Button>(i), EventType::SHORT_PRESS });
            }
        } else if (was_pressed && raw && !b.long_fired) {
            // Still held: check if we've crossed the long-press threshold.
            if (stable_ms >= BTN_LONG_MS) {
                b.long_fired = true;
                g_handler({ static_cast<Button>(i), EventType::LONG_PRESS });
            }
        }
    }
}

}  // namespace input
