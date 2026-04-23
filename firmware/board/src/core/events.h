#pragma once
// =============================================================================
// core/events.h — Inputs and outputs of the pure game StateMachine.
//
// The StateMachine receives `Event`s produced by the I/O layer (sensor
// debouncer, comm manager, timer) and emits `Effect`s that the I/O layer
// applies to hardware (LEDs, serial log, comm broadcasts).
//
// These types MUST NOT depend on Arduino, FastLED, NanoPB, or any proto
// generated code — the `core/` module is compiled both into the Arduino
// firmware and into the native simulation target.
// =============================================================================

#include <stdint.h>

namespace core {

// Mirrors catan.proto PlayerAction but lives in the pure-logic namespace so
// core/ doesn't depend on nanopb. Keep values in sync (they don't need to
// match numerically — the I/O shell translates).
enum class ActionKind : uint8_t {
    NONE = 0,
    READY,
    START_GAME,
    NEXT_NUMBER,
    PLACE_DONE,
    ROLL_DICE,
    END_TURN,
    SKIP_ROBBER,
    REPORT,
};

enum class EffectKind : uint8_t {
    NONE = 0,

    // Phase transition — a = new phase (cast from GamePhase).
    PHASE_ENTERED,

    // Lobby connected_mask changed — a = new mask.
    LOBBY_MASK_CHANGED,

    // Board layout was (re)randomized — no payload; caller re-reads g_tile_state.
    BOARD_RANDOMIZED,

    // Number reveal display — a = number (0 = clear highlight).
    REVEAL_NUMBER_CHANGED,

    // Placements were accepted and the underlying game state was mutated.
    //   a = player id, b = vertex/edge id.
    PLACED_SETTLEMENT,
    PLACED_CITY,
    PLACED_ROAD,

    // A placement or action was rejected.
    //   a = player id, b = reason (cast from RejectReason).
    PLACEMENT_REJECTED,

    // Dice roll. a = die1 (1-6), b = die2, c = 1 if total == 7 else 0.
    DICE_ROLLED,

    // Turn advanced normally — a = new current player id.
    TURN_ADVANCED,

    // Robber moved — a = new tile, b = old tile (0xFF if unset).
    ROBBER_MOVED,

    // Winner declared — a = player id.
    WINNER,
};

enum class RejectReason : uint8_t {
    NONE = 0,
    OUT_OF_TURN,
    WRONG_PHASE,
    VERTEX_OCCUPIED,
    TOO_CLOSE_TO_SETTLEMENT,   // distance-2 rule
    ROAD_OCCUPIED,
    ROAD_NOT_CONNECTED,
    NOT_MY_SETTLEMENT,
    ROBBER_SAME_TILE,
    INVALID_INDEX,
};

struct Effect {
    EffectKind kind = EffectKind::NONE;
    uint8_t    a    = 0;
    uint8_t    b    = 0;
    uint8_t    c    = 0;
};

}  // namespace core
