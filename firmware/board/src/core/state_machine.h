#pragma once
// =============================================================================
// core/state_machine.h — Pure game phase finite state machine.
//
// Consumes player actions, sensor presence notifications and ticks; emits
// effects (phase transitions, placement outcomes, dice rolls, robber moves,
// resource distributions, dev-card events, trades, winner). Mutates game
// state through `game::`; calls `core::rules` for validation.
//
// Host-compilable for the native simulation (firmware/board/native/sim_main.cpp).
// =============================================================================

#include <stdint.h>
#include "core/events.h"
#include "game_state.h"

namespace core {

class StateMachine {
public:
    static constexpr uint8_t EFFECT_QUEUE_SIZE = 64;

    StateMachine();

    void reset();

    // ── Event ingestion ─────────────────────────────────────────────────
    // Generic player action with full payload.
    void handlePlayerAction(uint8_t player, ActionKind action,
                            const ActionPayload& payload);

    // Convenience: action with no payload (READY toggle, START_GAME, etc.).
    void handlePlayerAction(uint8_t player, ActionKind action) {
        ActionPayload p;
        handlePlayerAction(player, action, p);
    }

    void onVertexPresent(uint8_t vertex);
    void onVertexAbsent(uint8_t vertex);
    void onEdgePresent(uint8_t edge);
    void onTilePresent(uint8_t tile);
    void tick(uint32_t now_ms);

    // ── Effect drain ────────────────────────────────────────────────────
    bool pollEffect(Effect& out);
    bool hasEffects() const;

    uint8_t firstPlayer() const { return first_player_; }

private:
    // Phase handlers
    void handleLobby_();
    void handleBoardSetup_();
    void handleNumberReveal_();
    void handleInitialPlacement_();
    void handlePlaying_();
    void handleRobber_();
    void handleDiscard_();
    void handleGameOver_();

    // Action handlers (called from handlePlayerAction).
    void onBuy_(uint8_t player, uint8_t kind);
    void onPlaceRobber_(uint8_t player, uint8_t tile);
    void onStealFrom_(uint8_t player, uint8_t target);
    void onDiscard_(uint8_t player, const uint8_t counts[5]);
    void onBankTrade_(uint8_t player, const uint8_t give[5], const uint8_t want[5]);
    void onTradeOffer_(uint8_t player, uint8_t target,
                       const uint8_t offer[5], const uint8_t want[5]);
    void onTradeAccept_(uint8_t acceptor);
    void onTradeDecline_(uint8_t player);
    void onTradeCancel_(uint8_t player);
    void onPlayKnight_(uint8_t player);
    void onPlayRoadBuilding_(uint8_t player);
    void onPlayYearOfPlenty_(uint8_t player, uint8_t r1, uint8_t r2);
    void onPlayMonopoly_(uint8_t player, uint8_t resource);

    // Helpers that both validate and commit.
    void tryPlaceSettlement_(uint8_t v, uint8_t player, bool initial);
    void tryUpgradeCity_(uint8_t v, uint8_t player);
    void tryPlaceRoad_(uint8_t e, uint8_t player, bool initial);
    void tryMoveRobber_(uint8_t tile);

    // Robber/discard transitions.
    void enterRobberOrDiscard_();
    void afterRobberPlacement_();
    void distributeInitialResources_(uint8_t player, uint8_t vertex);

    void recomputeAndEmitVp_();

    void setPhase_(GamePhase p);
    void pushEffect_(EffectKind k, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0);
    void emitReject_(uint8_t player, RejectReason r);

    // ── Queued inputs from the outside world ────────────────────────────
    bool          pending_start_game_  = false;
    bool          pending_next_number_ = false;
    ActionKind    pending_current_     = ActionKind::NONE;
    ActionPayload pending_current_payload_;

    // ── Ephemeral FSM state ────────────────────────────────────────────
    uint8_t first_player_     = 0;
    bool    board_setup_done_ = false;
    uint8_t last_lobby_mask_  = 0xFF;
    uint8_t last_reveal_num_  = 0xFF;
    uint8_t last_emitted_vp_[MAX_PLAYERS] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t last_largest_army_player_ = NO_PLAYER;
    uint8_t last_longest_road_player_ = NO_PLAYER;
    uint8_t last_longest_road_length_ = 0;

    // During the initial-placement second round, track which vertex each
    // player just placed so PLACE_DONE can distribute starting resources.
    uint8_t last_initial_vertex_ = NO_PLAYER;

    // 54 vertices fit in 64 bits.
    uint64_t pending_city_mask_ = 0;

    // Effect ring buffer.
    Effect  effects_[EFFECT_QUEUE_SIZE];
    uint8_t effect_head_ = 0;
    uint8_t effect_tail_ = 0;
};

}  // namespace core
