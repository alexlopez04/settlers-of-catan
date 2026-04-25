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

// Mirrors catan.proto PlayerAction. Values intentionally match the proto
// where possible to make the I/O shell's translation a no-op cast for new
// actions, and to keep diagnostics readable.
enum class ActionKind : uint8_t {
    NONE        = 0,
    READY       = 1,
    START_GAME  = 2,
    NEXT_NUMBER = 3,
    PLACE_DONE  = 4,
    ROLL_DICE   = 5,
    END_TURN    = 6,
    SKIP_ROBBER = 7,

    BUY_ROAD            = 10,
    BUY_SETTLEMENT      = 11,
    BUY_CITY            = 12,
    BUY_DEV_CARD        = 13,

    PLACE_ROBBER        = 14,
    STEAL_FROM          = 15,
    DISCARD             = 16,

    BANK_TRADE          = 17,
    TRADE_OFFER         = 18,
    TRADE_ACCEPT        = 19,
    TRADE_DECLINE       = 20,
    TRADE_CANCEL        = 21,

    PLAY_KNIGHT         = 22,
    PLAY_ROAD_BUILDING  = 23,
    PLAY_YEAR_OF_PLENTY = 24,
    PLAY_MONOPOLY       = 25,
};

// Action payload — flat parameter bundle decoupling the FSM from the proto
// PlayerInput message. Most actions only need a subset.
struct ActionPayload {
    uint8_t res[5]      = {0,0,0,0,0};   // generic resource counts (DISCARD, BANK_TRADE give, TRADE_OFFER offer)
    uint8_t want[5]     = {0,0,0,0,0};   // wanted resource counts (BANK_TRADE, TRADE_OFFER want)
    uint8_t target      = 0xFF;          // target player (STEAL, TRADE_OFFER) — 0xFF for "any"
    uint8_t robber_tile = 0xFF;          // PLACE_ROBBER destination
    uint8_t monopoly_res = 0;            // PLAY_MONOPOLY
    uint8_t card_res_1   = 0;            // PLAY_YEAR_OF_PLENTY
    uint8_t card_res_2   = 0;            // PLAY_YEAR_OF_PLENTY
    uint8_t aux          = 0;            // misc — e.g., READY toggle
};

enum class EffectKind : uint8_t {
    NONE = 0,

    PHASE_ENTERED,                 // a = new phase
    LOBBY_MASK_CHANGED,            // a = mask
    BOARD_RANDOMIZED,
    REVEAL_NUMBER_CHANGED,         // a = number (0=clear)
    PLACED_SETTLEMENT,             // a = player, b = vertex
    PLACED_CITY,                   // a = player, b = vertex
    PLACED_ROAD,                   // a = player, b = edge
    PLACEMENT_REJECTED,            // a = player, b = RejectReason
    DICE_ROLLED,                   // a = die1, b = die2, c = 1 if 7
    RESOURCES_DISTRIBUTED,         // a = roll, b = total cards dealt
    TURN_ADVANCED,                 // a = new player
    ROBBER_MOVED,                  // a = new tile, b = old tile
    DISCARD_REQUIRED,              // a = mask, b = (unused)
    DISCARD_COMPLETED,             // a = player, b = total discarded
    STEAL_OCCURRED,                // a = stealer, b = victim, c = resource (0..4)
    DEV_CARD_DRAWN,                // a = player, b = card type (Dev)
    KNIGHT_PLAYED,                 // a = player
    YEAR_OF_PLENTY_PLAYED,         // a = player
    MONOPOLY_PLAYED,               // a = player, b = resource, c = total taken
    ROAD_BUILDING_PLAYED,          // a = player
    LARGEST_ARMY_CHANGED,          // a = new player (NO_PLAYER if cleared)
    LONGEST_ROAD_CHANGED,          // a = new player, b = length
    TRADE_OFFERED,                 // a = from, b = to
    TRADE_ACCEPTED,                // a = from, b = acceptor
    TRADE_DECLINED,                // a = decliner
    TRADE_CANCELLED,
    BANK_TRADED,                   // a = player
    PURCHASE_MADE,                 // a = player, b = item kind (1=road,2=settle,3=city,4=devcard)
    VP_CHANGED,                    // a = player, b = new public VP
    WINNER,                        // a = player
};

enum class RejectReason : uint8_t {
    NONE = 0,
    OUT_OF_TURN,
    WRONG_PHASE,
    VERTEX_OCCUPIED,
    TOO_CLOSE_TO_SETTLEMENT,
    ROAD_OCCUPIED,
    ROAD_NOT_CONNECTED,
    NOT_MY_SETTLEMENT,
    ROBBER_SAME_TILE,
    INVALID_INDEX,
    NOT_PURCHASED,                 // 10 — placement attempted without prior BUY
    INSUFFICIENT_RESOURCES,        // 11
    PIECE_LIMIT_REACHED,           // 12
    BANK_DEPLETED,                 // 13 — bank cannot fulfil the request
    INVALID_TRADE,                 // 14
    NO_PENDING_TRADE,              // 15
    DEV_CARD_NOT_AVAILABLE,        // 16 — don't own / bought this turn / already played one
    DEV_DECK_EMPTY,                // 17
    INVALID_DISCARD,               // 18
    NOT_ELIGIBLE_TARGET,           // 19 — STEAL target not in eligible mask
};

struct Effect {
    EffectKind kind = EffectKind::NONE;
    uint8_t    a    = 0;
    uint8_t    b    = 0;
    uint8_t    c    = 0;
};

}  // namespace core
