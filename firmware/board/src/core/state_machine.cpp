// =============================================================================
// core/state_machine.cpp — Pure game FSM (see state_machine.h for contract).
// =============================================================================

#include "core/state_machine.h"
#include "core/rule_engine.h"
#include "core/rng.h"
#include "board_topology.h"
#include "config.h"

namespace core {

StateMachine::StateMachine() { reset(); }

void StateMachine::reset() {
    pending_start_game_  = false;
    pending_next_number_ = false;
    pending_current_     = ActionKind::NONE;
    pending_current_vp_  = 0;
    first_player_        = 0;
    board_setup_done_    = false;
    last_lobby_mask_     = 0xFF;
    last_reveal_num_     = 0xFF;
    effect_head_ = effect_tail_ = 0;
}

// ---------------------------------------------------------------------------
// Effect queue
// ---------------------------------------------------------------------------
void StateMachine::pushEffect_(EffectKind k, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t next = (uint8_t)((effect_tail_ + 1u) % EFFECT_QUEUE_SIZE);
    if (next == effect_head_) {
        // Overflow — drop the oldest to make room (better than silently
        // losing the newest game-state transition).
        effect_head_ = (uint8_t)((effect_head_ + 1u) % EFFECT_QUEUE_SIZE);
    }
    effects_[effect_tail_].kind = k;
    effects_[effect_tail_].a = a;
    effects_[effect_tail_].b = b;
    effects_[effect_tail_].c = c;
    effect_tail_ = next;
}

bool StateMachine::pollEffect(Effect& out) {
    if (effect_head_ == effect_tail_) return false;
    out = effects_[effect_head_];
    effect_head_ = (uint8_t)((effect_head_ + 1u) % EFFECT_QUEUE_SIZE);
    return true;
}

bool StateMachine::hasEffects() const {
    return effect_head_ != effect_tail_;
}

void StateMachine::setPhase_(GamePhase p) {
    game::setPhase(p);
    pushEffect_(EffectKind::PHASE_ENTERED, (uint8_t)p);
}

// ---------------------------------------------------------------------------
// Player action ingestion
// ---------------------------------------------------------------------------
void StateMachine::handlePlayerAction(uint8_t player, ActionKind action, uint8_t vp) {
    if (player >= MAX_PLAYERS) return;

    // Mark sender as connected and bump num_players on first contact.
    if (!game::playerConnected(player)) {
        game::setPlayerConnected(player, true);
        uint8_t new_n = 0;
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            if (game::playerConnected(p)) new_n = (uint8_t)(p + 1);
        }
        if (new_n > game::numPlayers()) game::setNumPlayers(new_n);
    }

    // Cache self-reported VP.
    if (action == ActionKind::REPORT) {
        game::setReportedVp(player, vp);
        return;
    }

    // Global triggers — any connected player may send these.
    if (action == ActionKind::START_GAME)  pending_start_game_  = true;
    if (action == ActionKind::NEXT_NUMBER) pending_next_number_ = true;

    // Current-player-only actions: capture for the tick handler.
    if (player == game::currentPlayer()) {
        if (action != ActionKind::NONE &&
            action != ActionKind::READY &&
            action != ActionKind::REPORT) {
            pending_current_    = action;
            pending_current_vp_ = vp;
        }
    }
}

// ---------------------------------------------------------------------------
// Sensor event ingestion
// ---------------------------------------------------------------------------
void StateMachine::onVertexPresent(uint8_t v) {
    if (v >= VERTEX_COUNT) return;
    const uint8_t cp = game::currentPlayer();

    switch (game::phase()) {
        case GamePhase::INITIAL_PLACEMENT:
            tryPlaceSettlement_(v, cp, /*initial=*/true);
            return;

        case GamePhase::PLAYING: {
            const VertexState& vs = game::vertexState(v);
            if (vs.owner == NO_PLAYER) {
                tryPlaceSettlement_(v, cp, /*initial=*/false);
            } else if (vs.owner == cp && !vs.is_city) {
                tryUpgradeCity_(v, cp);
            } else {
                pushEffect_(EffectKind::PLACEMENT_REJECTED, cp,
                            (uint8_t)RejectReason::VERTEX_OCCUPIED);
            }
            return;
        }

        default:
            // Other phases ignore vertex-sensor events.
            return;
    }
}

void StateMachine::onEdgePresent(uint8_t e) {
    if (e >= EDGE_COUNT) return;
    const uint8_t cp = game::currentPlayer();
    switch (game::phase()) {
        case GamePhase::INITIAL_PLACEMENT:
            tryPlaceRoad_(e, cp, /*initial=*/true);
            return;
        case GamePhase::PLAYING:
            tryPlaceRoad_(e, cp, /*initial=*/false);
            return;
        default:
            return;
    }
}

void StateMachine::onTilePresent(uint8_t t) {
    if (t >= TILE_COUNT) return;
    switch (game::phase()) {
        case GamePhase::PLAYING:
        case GamePhase::ROBBER:
            tryMoveRobber_(t);
            if (game::phase() == GamePhase::ROBBER) {
                // In the ROBBER phase, placing the robber returns control
                // to PLAYING immediately.
                setPhase_(GamePhase::PLAYING);
            }
            return;
        default:
            return;
    }
}

// ---------------------------------------------------------------------------
// Placement helpers (validate → commit → emit)
// ---------------------------------------------------------------------------
void StateMachine::tryPlaceSettlement_(uint8_t v, uint8_t player, bool initial) {
    RejectReason r = rules::validateSettlement(v, player, initial);
    if (r != RejectReason::NONE) {
        pushEffect_(EffectKind::PLACEMENT_REJECTED, player, (uint8_t)r);
        return;
    }
    game::placeSettlement(v, player);
    pushEffect_(EffectKind::PLACED_SETTLEMENT, player, v);
}

void StateMachine::tryUpgradeCity_(uint8_t v, uint8_t player) {
    RejectReason r = rules::validateCity(v, player);
    if (r != RejectReason::NONE) {
        pushEffect_(EffectKind::PLACEMENT_REJECTED, player, (uint8_t)r);
        return;
    }
    game::upgradeToCity(v);
    pushEffect_(EffectKind::PLACED_CITY, player, v);
}

void StateMachine::tryPlaceRoad_(uint8_t e, uint8_t player, bool initial) {
    RejectReason r = rules::validateRoad(e, player, initial);
    if (r != RejectReason::NONE) {
        pushEffect_(EffectKind::PLACEMENT_REJECTED, player, (uint8_t)r);
        return;
    }
    game::placeRoad(e, player);
    pushEffect_(EffectKind::PLACED_ROAD, player, e);
}

void StateMachine::tryMoveRobber_(uint8_t tile) {
    RejectReason r = rules::validateRobberMove(tile);
    if (r != RejectReason::NONE) {
        pushEffect_(EffectKind::PLACEMENT_REJECTED, game::currentPlayer(),
                    (uint8_t)r);
        return;
    }
    uint8_t old = game::robberTile();
    game::setRobberTile(tile);
    pushEffect_(EffectKind::ROBBER_MOVED, tile, old);
}

// ---------------------------------------------------------------------------
// Tick — per-phase dispatch on pending_* flags
// ---------------------------------------------------------------------------
void StateMachine::tick(uint32_t /*now_ms*/) {
    switch (game::phase()) {
        case GamePhase::LOBBY:             handleLobby_();            break;
        case GamePhase::BOARD_SETUP:       handleBoardSetup_();       break;
        case GamePhase::NUMBER_REVEAL:     handleNumberReveal_();     break;
        case GamePhase::INITIAL_PLACEMENT: handleInitialPlacement_(); break;
        case GamePhase::PLAYING:           handlePlaying_();          break;
        case GamePhase::ROBBER:            handleRobber_();           break;
        case GamePhase::GAME_OVER:         handleGameOver_();         break;
    }
}

// ---------------------------------------------------------------------------
// Phase handlers
// ---------------------------------------------------------------------------
void StateMachine::handleLobby_() {
    uint8_t mask = game::connectedMask();
    if (mask != last_lobby_mask_) {
        last_lobby_mask_ = mask;
        pushEffect_(EffectKind::LOBBY_MASK_CHANGED, mask);
    }

    if (pending_start_game_ && game::numPlayers() >= MIN_PLAYERS) {
        pending_start_game_ = false;

        // Randomly pick a starting player from the connected set.
        uint8_t ids[MAX_PLAYERS];
        uint8_t cnt = 0;
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (game::playerConnected(i)) ids[cnt++] = i;
        }
        // Use the RNG facade (native sim is deterministic if seeded).
        if (cnt > 0) {
            first_player_ = ids[core::rng::uniform(cnt)];
        } else {
            first_player_ = 0;
        }

        board_setup_done_ = false;
        setPhase_(GamePhase::BOARD_SETUP);
    }
}

void StateMachine::handleBoardSetup_() {
    if (!board_setup_done_) {
        board_setup_done_ = true;
        randomizeBoardLayout();

        // Put the robber on the desert tile.
        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            if (g_tile_state[t].biome == Biome::DESERT) {
                game::setRobberTile(t);
                break;
            }
        }
        pushEffect_(EffectKind::BOARD_RANDOMIZED);
    }

    if (pending_next_number_) {
        pending_next_number_ = false;
        game::resetReveal();
        last_reveal_num_ = 0xFF;
        setPhase_(GamePhase::NUMBER_REVEAL);
    }
}

void StateMachine::handleNumberReveal_() {
    uint8_t num = game::currentRevealNumber();
    if (num != last_reveal_num_) {
        last_reveal_num_ = num;
        pushEffect_(EffectKind::REVEAL_NUMBER_CHANGED, num);
    }

    if (pending_next_number_) {
        pending_next_number_ = false;
        if (!game::advanceReveal()) {
            // All numbers revealed — start initial placement.
            last_reveal_num_ = 0xFF;
            pushEffect_(EffectKind::REVEAL_NUMBER_CHANGED, 0);  // clear
            game::resetSetupRound(first_player_);
            setPhase_(GamePhase::INITIAL_PLACEMENT);
        }
    }
}

void StateMachine::handleInitialPlacement_() {
    // Piece placement arrives via onVertexPresent / onEdgePresent. The only
    // tick-level responsibility is handling the PLACE_DONE confirmation.
    if (pending_current_ == ActionKind::PLACE_DONE) {
        pending_current_ = ActionKind::NONE;
        if (!game::advanceSetupTurn()) {
            // All 2*N setup turns complete — enter play from first player.
            game::setCurrentPlayer(first_player_);
            game::clearDice();
            setPhase_(GamePhase::PLAYING);
        } else {
            pushEffect_(EffectKind::TURN_ADVANCED, game::currentPlayer());
        }
    }
}

void StateMachine::handlePlaying_() {
    // Dice roll
    if (pending_current_ == ActionKind::ROLL_DICE && !game::hasRolled()) {
        pending_current_ = ActionKind::NONE;
        game::rollDice();
        uint8_t d1 = game::lastDie1();
        uint8_t d2 = game::lastDie2();
        uint8_t total = (uint8_t)(d1 + d2);
        uint8_t triggers_robber = (total == ROBBER_ROLL) ? 1 : 0;
        pushEffect_(EffectKind::DICE_ROLLED, d1, d2, triggers_robber);
        if (triggers_robber) {
            setPhase_(GamePhase::ROBBER);
            return;
        }
    }

    // End turn
    if (pending_current_ == ActionKind::END_TURN && game::hasRolled()) {
        pending_current_ = ActionKind::NONE;
        game::nextTurn();
        pushEffect_(EffectKind::TURN_ADVANCED, game::currentPlayer());
    }

    // Winner check (self-reported VP)
    uint8_t w = game::checkWinner();
    if (w != NO_PLAYER) {
        pushEffect_(EffectKind::WINNER, w);
        setPhase_(GamePhase::GAME_OVER);
    }
}

void StateMachine::handleRobber_() {
    // Robber placement happens via onTilePresent which transitions back
    // to PLAYING. This handler only services SKIP_ROBBER.
    if (pending_current_ == ActionKind::SKIP_ROBBER) {
        pending_current_ = ActionKind::NONE;
        setPhase_(GamePhase::PLAYING);
    }
}

void StateMachine::handleGameOver_() {
    // Nothing to do — the I/O shell handles the victory blink.
    // Clear any lingering pending actions so a stale button press
    // doesn't trigger spurious transitions if the game resumes.
    pending_current_     = ActionKind::NONE;
    pending_start_game_  = false;
    pending_next_number_ = false;
}

}  // namespace core
