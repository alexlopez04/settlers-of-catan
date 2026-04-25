#pragma once
// =============================================================================
// input.h — Debounced button driver with short-press and long-press events.
//
// Three buttons (A / B / C) are polled every loop tick.
//
// Callers register an EventHandler and call input::tick() from loop().
// Events are delivered synchronously from within tick().
// =============================================================================

#include <stdint.h>

namespace input {

// ── Event types ──────────────────────────────────────────────────────────────

enum class Button : uint8_t { A = 0, B = 1, C = 2 };

enum class EventType : uint8_t {
    SHORT_PRESS,  // Button pressed and released within BTN_LONG_MS
    LONG_PRESS,   // Button held for at least BTN_LONG_MS (fires once, on hold)
};

struct Event {
    Button    button;
    EventType type;
};

typedef void (*EventHandler)(const Event& evt);

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Configure GPIO pins, enable internal pull-ups, register the event callback.
void init(EventHandler handler);

// Poll all buttons and fire events. Call from loop() as fast as possible.
void tick();

}  // namespace input
