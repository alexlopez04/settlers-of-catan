#pragma once
// =============================================================================
// core/state_machine.h — Pure game phase finite state machine.
//
// Consumes events (player actions, sensor presence notifications, periodic
// ticks) and emits effects (phase transitions, placement outcomes, dice
// rolls, robber moves, LED cues). Mutates game state through the `game::`
// namespace; calls `core::rules` to validate placements; never touches
// Arduino / LED / comm / sensor managers directly.
//
// Everything in this file is host-compilable for simulation. The I/O shell
// (firmware/board/src/main.cpp) is responsible for:
//   - decoding sensor/LoRa traffic into Events,
//   - translating emitted Effects into hardware side-effects,
//   - broadcasting BoardState to the bridge.
// =============================================================================

#include <stdint.h>
#include "core/events.h"
#include "game_state.h"   // for GamePhase enum

namespace core {

class StateMachine {
public:
    static constexpr uint8_t EFFECT_QUEUE_SIZE = 32;

    StateMachine();

    // Full reset — call after `game::init()` before starting a new run.
    void reset();

    // ── Event ingestion ─────────────────────────────────────────────────
    void handlePlayerAction(uint8_t player, ActionKind action, uint8_t vp = 0);
    void onVertexPresent(uint8_t vertex);
    void onEdgePresent(uint8_t edge);
    void onTilePresent(uint8_t tile);
    void tick(uint32_t now_ms);

    // ── Effect drain ────────────────────────────────────────────────────
    bool pollEffect(Effect& out);
    bool hasEffects() const;

    // ── Observation (mostly for tests / diagnostics) ────────────────────
    uint8_t firstPlayer() const { return first_player_; }

private:
    void handleLobby_();
    void handleBoardSetup_();
    void handleNumberReveal_();
    void handleInitialPlacement_();
    void handlePlaying_();
    void handleRobber_();
    void handleGameOver_();

    // Apply a validated action from a player whose turn it is right now.
    void runCurrentAction_();

    // Helpers that both validate and commit.
    void tryPlaceSettlement_(uint8_t v, uint8_t player, bool initial);
    void tryUpgradeCity_(uint8_t v, uint8_t player);
    void tryPlaceRoad_(uint8_t e, uint8_t player, bool initial);
    void tryMoveRobber_(uint8_t tile);

    void setPhase_(GamePhase p);
    void pushEffect_(EffectKind k, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0);

    // ── Queued inputs from the outside world ────────────────────────────
    bool       pending_start_game_   = false;
    bool       pending_next_number_  = false;
    ActionKind pending_current_      = ActionKind::NONE;
    uint8_t    pending_current_vp_   = 0;

    // ── Ephemeral FSM state formerly in board/main.cpp ─────────────────
    uint8_t first_player_     = 0;
    bool    board_setup_done_ = false;
    uint8_t last_lobby_mask_  = 0xFF;   // force initial emit
    uint8_t last_reveal_num_  = 0xFF;

    // ── Effect ring buffer (power-of-two for cheap masking) ─────────────
    Effect  effects_[EFFECT_QUEUE_SIZE];
    uint8_t effect_head_ = 0;   // read index
    uint8_t effect_tail_ = 0;   // write index
};

}  // namespace core
