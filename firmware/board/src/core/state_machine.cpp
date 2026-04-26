// =============================================================================
// core/state_machine.cpp — Pure game FSM (see state_machine.h).
// =============================================================================

#include "core/state_machine.h"
#include "core/rule_engine.h"
#include "core/rng.h"
#include "board_topology.h"
#include "config.h"
#include <string.h>

namespace core {

StateMachine::StateMachine() { reset(); }

void StateMachine::reset() {
    pending_start_game_  = false;
    pending_next_number_ = false;
    pending_current_     = ActionKind::NONE;
    pending_current_payload_ = ActionPayload();
    first_player_        = 0;
    board_setup_done_    = false;
    last_lobby_mask_     = 0xFF;
    last_reveal_num_     = 0xFF;
    pending_city_mask_   = 0;
    last_initial_vertex_ = NO_PLAYER;
    last_largest_army_player_ = NO_PLAYER;
    last_longest_road_player_ = NO_PLAYER;
    last_longest_road_length_ = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) last_emitted_vp_[i] = 0xFF;
    effect_head_ = effect_tail_ = 0;
}

// ── Effect queue ───────────────────────────────────────────────────────────
void StateMachine::pushEffect_(EffectKind k, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t next = (uint8_t)((effect_tail_ + 1u) % EFFECT_QUEUE_SIZE);
    if (next == effect_head_) {
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
bool StateMachine::hasEffects() const { return effect_head_ != effect_tail_; }

void StateMachine::setPhase_(GamePhase p) {
    game::setPhase(p);
    pending_city_mask_ = 0;
    pushEffect_(EffectKind::PHASE_ENTERED, (uint8_t)p);
}

void StateMachine::emitReject_(uint8_t player, RejectReason r) {
    if (r == RejectReason::NONE) return;
    pushEffect_(EffectKind::PLACEMENT_REJECTED, player, (uint8_t)r);
    game::setLastRejectReason((uint8_t)r);
}

// ── VP change emission ─────────────────────────────────────────────────────
void StateMachine::recomputeAndEmitVp_() {
    game::recomputeVp();

    uint8_t la = game::largestArmyPlayer();
    if (la != last_largest_army_player_) {
        last_largest_army_player_ = la;
        pushEffect_(EffectKind::LARGEST_ARMY_CHANGED, la);
    }
    uint8_t lr = game::longestRoadPlayer();
    uint8_t lr_len = game::longestRoadLength();
    if (lr != last_longest_road_player_ || lr_len != last_longest_road_length_) {
        last_longest_road_player_ = lr;
        last_longest_road_length_ = lr_len;
        pushEffect_(EffectKind::LONGEST_ROAD_CHANGED, lr, lr_len);
    }
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        uint8_t v = game::publicVp(p);
        if (v != last_emitted_vp_[p]) {
            last_emitted_vp_[p] = v;
            pushEffect_(EffectKind::VP_CHANGED, p, v);
        }
    }
    uint8_t w = game::checkWinner();
    if (w != NO_PLAYER && game::phase() != GamePhase::GAME_OVER) {
        pushEffect_(EffectKind::WINNER, w);
        setPhase_(GamePhase::GAME_OVER);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Action ingestion
// ──────────────────────────────────────────────────────────────────────────
void StateMachine::handlePlayerAction(uint8_t player, ActionKind action,
                                      const ActionPayload& payload) {
    if (player >= MAX_PLAYERS) return;

    // READY toggle — `aux` overload from caller: 0=clear, 1=set, 0xFF=toggle.
    if (action == ActionKind::READY) {
        bool now = game::playerReady(player);
        if (payload.aux == 0xFF) game::setPlayerReady(player, !now);
        else                     game::setPlayerReady(player, payload.aux != 0);
        return;
    }

    if (action == ActionKind::START_GAME)  { pending_start_game_  = true; return; }
    if (action == ActionKind::NEXT_NUMBER) { pending_next_number_ = true; return; }

    // DISCARD is allowed from any player whose bit is in the mask.
    if (action == ActionKind::DISCARD) {
        onDiscard_(player, payload.res);
        return;
    }

    // TRADE_ACCEPT / DECLINE — opponents (not current player) can answer.
    if (action == ActionKind::TRADE_ACCEPT) { onTradeAccept_(player); return; }
    if (action == ActionKind::TRADE_DECLINE) { onTradeDecline_(player); return; }

    // All other actions require the sender to be the current player.
    if (player != game::currentPlayer()) {
        emitReject_(player, RejectReason::OUT_OF_TURN);
        return;
    }

    switch (action) {
        case ActionKind::PLACE_DONE:
        case ActionKind::ROLL_DICE:
        case ActionKind::END_TURN:
        case ActionKind::SKIP_ROBBER:
            pending_current_         = action;
            pending_current_payload_ = payload;
            return;

        case ActionKind::BUY_ROAD:           onBuy_(player, 1); return;
        case ActionKind::BUY_SETTLEMENT:     onBuy_(player, 2); return;
        case ActionKind::BUY_CITY:           onBuy_(player, 3); return;
        case ActionKind::BUY_DEV_CARD:       onBuy_(player, 4); return;

        case ActionKind::PLACE_ROBBER:
            onPlaceRobber_(player, payload.robber_tile); return;
        case ActionKind::STEAL_FROM:
            onStealFrom_(player, payload.target); return;

        case ActionKind::BANK_TRADE:
            onBankTrade_(player, payload.res, payload.want); return;
        case ActionKind::TRADE_OFFER:
            onTradeOffer_(player, payload.target, payload.res, payload.want); return;
        case ActionKind::TRADE_CANCEL:
            onTradeCancel_(player); return;

        case ActionKind::PLAY_KNIGHT:
            onPlayKnight_(player); return;
        case ActionKind::PLAY_ROAD_BUILDING:
            onPlayRoadBuilding_(player); return;
        case ActionKind::PLAY_YEAR_OF_PLENTY:
            onPlayYearOfPlenty_(player, payload.card_res_1, payload.card_res_2); return;
        case ActionKind::PLAY_MONOPOLY:
            onPlayMonopoly_(player, payload.monopoly_res); return;

        default:
            return;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Sensor events
// ──────────────────────────────────────────────────────────────────────────
void StateMachine::onVertexAbsent(uint8_t v) {
    if (v >= VERTEX_COUNT) return;
    const uint64_t bit = (1ULL << v);
    if (game::phase() != GamePhase::PLAYING) {
        pending_city_mask_ &= ~bit;
        return;
    }
    const VertexState& vs = game::vertexState(v);
    const uint8_t cp = game::currentPlayer();
    if (vs.owner == cp && !vs.is_city) {
        pending_city_mask_ |= bit;
    } else {
        pending_city_mask_ &= ~bit;
    }
}

void StateMachine::onVertexPresent(uint8_t v) {
    if (v >= VERTEX_COUNT) return;
    const uint8_t cp = game::currentPlayer();

    switch (game::phase()) {
        case GamePhase::INITIAL_PLACEMENT:
            tryPlaceSettlement_(v, cp, /*initial=*/true);
            return;

        case GamePhase::PLAYING: {
            const VertexState& vs = game::vertexState(v);
            const uint64_t bit = (1ULL << v);
            if (vs.owner == NO_PLAYER) {
                pending_city_mask_ &= ~bit;
                tryPlaceSettlement_(v, cp, /*initial=*/false);
            } else if (vs.owner == cp && !vs.is_city &&
                       (pending_city_mask_ & bit)) {
                pending_city_mask_ &= ~bit;
                tryUpgradeCity_(v, cp);
            } else {
                pending_city_mask_ &= ~bit;
                emitReject_(cp, RejectReason::VERTEX_OCCUPIED);
            }
            return;
        }
        default:
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
    if (game::phase() == GamePhase::ROBBER) {
        // Only treat tile-sensor events as robber moves while we're awaiting
        // robber placement. The active player must place via either the
        // physical tile sensor or ACTION_PLACE_ROBBER.
        onPlaceRobber_(game::currentPlayer(), t);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Placement helpers
// ──────────────────────────────────────────────────────────────────────────
void StateMachine::tryPlaceSettlement_(uint8_t v, uint8_t player, bool initial) {
    // During initial placement each player may place exactly one settlement
    // per turn (1 in round 1, 2 total by end of round 2).
    if (initial && game::settlementCount(player) >= game::setupRound()) {
        emitReject_(player, RejectReason::SETUP_TURN_LIMIT);
        return;
    }
    RejectReason r = rules::validateSettlement(v, player, initial);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }

    if (!initial) {
        // Must have prepaid for this placement.
        if (game::pendingSettlementBuy(player) == 0) {
            emitReject_(player, RejectReason::NOT_PURCHASED);
            return;
        }
        game::consumePendingSettlementBuy(player);
    }

    game::placeSettlement(v, player);
    pushEffect_(EffectKind::PLACED_SETTLEMENT, player, v);

    if (initial && game::setupRound() == 2) {
        last_initial_vertex_ = v;
    }
    recomputeAndEmitVp_();
}

void StateMachine::tryUpgradeCity_(uint8_t v, uint8_t player) {
    RejectReason r = rules::validateCity(v, player);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }

    if (game::pendingCityBuy(player) == 0) {
        emitReject_(player, RejectReason::NOT_PURCHASED);
        return;
    }
    game::consumePendingCityBuy(player);
    game::upgradeToCity(v);
    pushEffect_(EffectKind::PLACED_CITY, player, v);
    recomputeAndEmitVp_();
}

void StateMachine::tryPlaceRoad_(uint8_t e, uint8_t player, bool initial) {
    // During initial placement each player may place exactly one road per turn.
    if (initial && game::roadCount(player) >= game::setupRound()) {
        emitReject_(player, RejectReason::SETUP_TURN_LIMIT);
        return;
    }
    RejectReason r = rules::validateRoad(e, player, initial);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }

    if (!initial) {
        if (game::freeRoadsRemaining(player) > 0) {
            game::setFreeRoadsRemaining(player, (uint8_t)(game::freeRoadsRemaining(player) - 1));
        } else if (game::pendingRoadBuy(player) > 0) {
            game::consumePendingRoadBuy(player);
        } else {
            emitReject_(player, RejectReason::NOT_PURCHASED);
            return;
        }
    }

    game::placeRoad(e, player);
    pushEffect_(EffectKind::PLACED_ROAD, player, e);
    recomputeAndEmitVp_();
}

void StateMachine::tryMoveRobber_(uint8_t tile) {
    RejectReason r = rules::validateRobberMove(tile);
    if (r != RejectReason::NONE) {
        emitReject_(game::currentPlayer(), r);
        return;
    }
    uint8_t old = game::robberTile();
    game::setRobberTile(tile);
    pushEffect_(EffectKind::ROBBER_MOVED, tile, old);
}

// ──────────────────────────────────────────────────────────────────────────
// Action handlers
// ──────────────────────────────────────────────────────────────────────────
void StateMachine::onBuy_(uint8_t player, uint8_t kind) {
    if (game::phase() != GamePhase::PLAYING) {
        emitReject_(player, RejectReason::WRONG_PHASE);
        return;
    }
    if (!game::hasRolled() && kind != 4 /*dev card requires roll too in standard rules*/) {
        emitReject_(player, RejectReason::WRONG_PHASE);
        return;
    }
    RejectReason r = rules::validatePurchase(player, kind);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }

    switch (kind) {
        case 1:
            game::spendCost(player, ROAD_COST);
            game::addPendingRoadBuy(player);
            pushEffect_(EffectKind::PURCHASE_MADE, player, 1);
            break;
        case 2:
            game::spendCost(player, SETTLEMENT_COST);
            game::addPendingSettlementBuy(player);
            pushEffect_(EffectKind::PURCHASE_MADE, player, 2);
            break;
        case 3:
            game::spendCost(player, CITY_COST);
            game::addPendingCityBuy(player);
            pushEffect_(EffectKind::PURCHASE_MADE, player, 3);
            break;
        case 4: {
            game::spendCost(player, DEV_CARD_COST);
            Dev d = game::drawDevCard(player);
            if (d == Dev::COUNT) {
                // Refund — should have been caught by validation.
                game::refundCost(player, DEV_CARD_COST);
                emitReject_(player, RejectReason::DEV_DECK_EMPTY);
                return;
            }
            pushEffect_(EffectKind::PURCHASE_MADE, player, 4);
            pushEffect_(EffectKind::DEV_CARD_DRAWN, player, (uint8_t)d);
            recomputeAndEmitVp_();   // VP cards count immediately for the holder
            break;
        }
    }
}

void StateMachine::onPlaceRobber_(uint8_t player, uint8_t tile) {
    if (game::phase() != GamePhase::ROBBER) {
        emitReject_(player, RejectReason::WRONG_PHASE);
        return;
    }
    RejectReason r = rules::validateRobberMove(tile);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }
    uint8_t old = game::robberTile();
    game::setRobberTile(tile);
    pushEffect_(EffectKind::ROBBER_MOVED, tile, old);
    afterRobberPlacement_();
}

void StateMachine::afterRobberPlacement_() {
    uint8_t mask = game::recomputeStealEligibleMask();
    if (mask == 0) {
        // No targets — return to PLAYING immediately.
        setPhase_(GamePhase::PLAYING);
    }
    // else: stay in ROBBER phase; client must send STEAL_FROM next.
}

void StateMachine::onStealFrom_(uint8_t player, uint8_t target) {
    if (game::phase() != GamePhase::ROBBER) {
        emitReject_(player, RejectReason::WRONG_PHASE);
        return;
    }
    uint8_t mask = game::stealEligibleMask();
    if (mask == 0) {
        // Allow SKIP_ROBBER style transition: just exit.
        setPhase_(GamePhase::PLAYING);
        return;
    }
    if (target >= MAX_PLAYERS || !(mask & (1u << target))) {
        emitReject_(player, RejectReason::NOT_ELIGIBLE_TARGET);
        return;
    }
    // Pick a uniformly-random card type from victim's hand.
    uint8_t total = game::totalCards(target);
    if (total == 0) {
        emitReject_(player, RejectReason::NOT_ELIGIBLE_TARGET);
        return;
    }
    uint8_t pick = (uint8_t)core::rng::uniform(total);
    uint8_t chosen = 0;
    for (uint8_t r = 0; r < 5; ++r) {
        uint8_t n = game::resCount(target, (Res)r);
        if (pick < n) { chosen = r; break; }
        pick = (uint8_t)(pick - n);
    }
    // Transfer one card directly (does not go through bank).
    game::setResCount(target, (Res)chosen, (uint8_t)(game::resCount(target, (Res)chosen) - 1));
    game::addRes(player, (Res)chosen, 1);
    pushEffect_(EffectKind::STEAL_OCCURRED, player, target, chosen);
    setPhase_(GamePhase::PLAYING);
}

void StateMachine::onDiscard_(uint8_t player, const uint8_t counts[5]) {
    if (game::phase() != GamePhase::DISCARD) {
        emitReject_(player, RejectReason::WRONG_PHASE);
        return;
    }
    RejectReason r = rules::validateDiscard(player, counts);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }
    uint8_t total_discarded = 0;
    for (uint8_t i = 0; i < 5; ++i) {
        if (counts[i]) {
            game::spendRes(player, (Res)i, counts[i]);
            total_discarded = (uint8_t)(total_discarded + counts[i]);
        }
    }
    game::clearDiscardRequired(player);
    pushEffect_(EffectKind::DISCARD_COMPLETED, player, total_discarded);
    if (game::discardRequiredMask() == 0) {
        // Transition to ROBBER phase — current player places robber.
        setPhase_(GamePhase::ROBBER);
    }
}

void StateMachine::onBankTrade_(uint8_t player, const uint8_t give[5], const uint8_t want[5]) {
    if (game::phase() != GamePhase::PLAYING || !game::hasRolled()) {
        emitReject_(player, RejectReason::WRONG_PHASE);
        return;
    }
    if (player != game::currentPlayer()) {
        emitReject_(player, RejectReason::OUT_OF_TURN);
        return;
    }
    RejectReason r = rules::validateBankTrade(player, give, want);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }
    for (uint8_t i = 0; i < 5; ++i) if (give[i]) game::spendRes(player, (Res)i, give[i]);
    for (uint8_t i = 0; i < 5; ++i) if (want[i]) {
        game::drawFromBank((Res)i, want[i]);
        game::addRes(player, (Res)i, want[i]);
    }
    pushEffect_(EffectKind::BANK_TRADED, player);
}

void StateMachine::onTradeOffer_(uint8_t player, uint8_t target,
                                 const uint8_t offer[5], const uint8_t want[5]) {
    RejectReason r = rules::validateTradeOffer(player, offer, want);
    if (r != RejectReason::NONE) { emitReject_(player, r); return; }
    uint8_t to = (target < MAX_PLAYERS) ? target : NO_PLAYER;
    game::setPendingTrade(player, to, offer, want);
    pushEffect_(EffectKind::TRADE_OFFERED, player, (to == NO_PLAYER) ? 0xFF : to);
}

void StateMachine::onTradeAccept_(uint8_t acceptor) {
    if (game::phase() != GamePhase::PLAYING) {
        emitReject_(acceptor, RejectReason::WRONG_PHASE);
        return;
    }
    RejectReason r = rules::validateTradeAccept(acceptor);
    if (r != RejectReason::NONE) { emitReject_(acceptor, r); return; }
    const auto& t = game::pendingTrade();
    uint8_t from = t.from;
    // Verify offerer still has offered resources (could have changed since offer).
    for (uint8_t i = 0; i < 5; ++i) {
        if (game::resCount(from, (Res)i) < t.offer[i]) {
            emitReject_(acceptor, RejectReason::INVALID_TRADE);
            game::clearPendingTrade();
            return;
        }
    }
    // Swap directly (does not touch bank).
    for (uint8_t i = 0; i < 5; ++i) {
        if (t.offer[i]) {
            game::setResCount(from, (Res)i, (uint8_t)(game::resCount(from, (Res)i) - t.offer[i]));
            game::addRes(acceptor, (Res)i, t.offer[i]);
        }
        if (t.want[i]) {
            game::setResCount(acceptor, (Res)i, (uint8_t)(game::resCount(acceptor, (Res)i) - t.want[i]));
            game::addRes(from, (Res)i, t.want[i]);
        }
    }
    pushEffect_(EffectKind::TRADE_ACCEPTED, from, acceptor);
    game::clearPendingTrade();
}

void StateMachine::onTradeDecline_(uint8_t player) {
    if (!game::hasPendingTrade()) return;
    const auto& t = game::pendingTrade();
    if (t.to != NO_PLAYER && t.to != player) return;   // ignore unrelated declines
    pushEffect_(EffectKind::TRADE_DECLINED, player);
    if (t.to != NO_PLAYER) game::clearPendingTrade();
    // For open offers, declines are advisory; current player must explicitly cancel.
}

void StateMachine::onTradeCancel_(uint8_t player) {
    if (!game::hasPendingTrade()) return;
    if (game::pendingTrade().from != player) {
        emitReject_(player, RejectReason::OUT_OF_TURN);
        return;
    }
    game::clearPendingTrade();
    pushEffect_(EffectKind::TRADE_CANCELLED);
}

// ── Dev card plays ────────────────────────────────────────────────────────
void StateMachine::onPlayKnight_(uint8_t player) {
    if (!game::canPlayDevCard(player, Dev::KNIGHT)) {
        emitReject_(player, RejectReason::DEV_CARD_NOT_AVAILABLE);
        return;
    }
    game::setDevCardCount(player, Dev::KNIGHT,
                          (uint8_t)(game::devCardCount(player, Dev::KNIGHT) - 1));
    game::setCardPlayedThisTurn(true);
    game::incKnightsPlayed(player);
    pushEffect_(EffectKind::KNIGHT_PLAYED, player);
    recomputeAndEmitVp_();
    // Knight triggers robber move WITHOUT discard step.
    setPhase_(GamePhase::ROBBER);
}

void StateMachine::onPlayRoadBuilding_(uint8_t player) {
    if (!game::canPlayDevCard(player, Dev::ROAD_BUILDING)) {
        emitReject_(player, RejectReason::DEV_CARD_NOT_AVAILABLE);
        return;
    }
    game::setDevCardCount(player, Dev::ROAD_BUILDING,
                          (uint8_t)(game::devCardCount(player, Dev::ROAD_BUILDING) - 1));
    game::setCardPlayedThisTurn(true);
    // Grant up to two free roads (capped by remaining road pieces).
    uint8_t avail = (uint8_t)(MAX_ROADS_PER_PLAYER - game::roadCount(player));
    if (avail > 2) avail = 2;
    game::setFreeRoadsRemaining(player, avail);
    pushEffect_(EffectKind::ROAD_BUILDING_PLAYED, player);
}

void StateMachine::onPlayYearOfPlenty_(uint8_t player, uint8_t r1, uint8_t r2) {
    if (!game::canPlayDevCard(player, Dev::YEAR_OF_PLENTY)) {
        emitReject_(player, RejectReason::DEV_CARD_NOT_AVAILABLE);
        return;
    }
    if (r1 >= 5 || r2 >= 5) { emitReject_(player, RejectReason::INVALID_INDEX); return; }
    if (game::bankSupply((Res)r1) == 0 || (r1 == r2 && game::bankSupply((Res)r1) < 2) ||
        (r1 != r2 && game::bankSupply((Res)r2) == 0)) {
        emitReject_(player, RejectReason::BANK_DEPLETED);
        return;
    }
    game::setDevCardCount(player, Dev::YEAR_OF_PLENTY,
                          (uint8_t)(game::devCardCount(player, Dev::YEAR_OF_PLENTY) - 1));
    game::setCardPlayedThisTurn(true);
    game::drawFromBank((Res)r1, 1); game::addRes(player, (Res)r1, 1);
    game::drawFromBank((Res)r2, 1); game::addRes(player, (Res)r2, 1);
    pushEffect_(EffectKind::YEAR_OF_PLENTY_PLAYED, player);
}

void StateMachine::onPlayMonopoly_(uint8_t player, uint8_t resource) {
    if (!game::canPlayDevCard(player, Dev::MONOPOLY)) {
        emitReject_(player, RejectReason::DEV_CARD_NOT_AVAILABLE);
        return;
    }
    if (resource >= 5) { emitReject_(player, RejectReason::INVALID_INDEX); return; }
    game::setDevCardCount(player, Dev::MONOPOLY,
                          (uint8_t)(game::devCardCount(player, Dev::MONOPOLY) - 1));
    game::setCardPlayedThisTurn(true);
    uint16_t taken = 0;
    for (uint8_t p = 0; p < game::numPlayers(); ++p) {
        if (p == player) continue;
        uint8_t n = game::resCount(p, (Res)resource);
        if (!n) continue;
        game::setResCount(p, (Res)resource, 0);
        taken += n;
    }
    if (taken) game::addRes(player, (Res)resource, (uint8_t)((taken > 255) ? 255 : taken));
    pushEffect_(EffectKind::MONOPOLY_PLAYED, player, resource,
                (uint8_t)((taken > 255) ? 255 : taken));
}

// ──────────────────────────────────────────────────────────────────────────
// Robber / discard transitions
// ──────────────────────────────────────────────────────────────────────────
void StateMachine::enterRobberOrDiscard_() {
    // Compute discard requirements.
    game::clearAllDiscardRequired();
    bool any = false;
    for (uint8_t p = 0; p < game::numPlayers(); ++p) {
        uint8_t n = game::totalCards(p);
        if (n > DISCARD_THRESHOLD) {
            game::setDiscardRequired(p, (uint8_t)(n / 2));
            any = true;
        }
    }
    if (any) {
        pushEffect_(EffectKind::DISCARD_REQUIRED, game::discardRequiredMask());
        setPhase_(GamePhase::DISCARD);
    } else {
        setPhase_(GamePhase::ROBBER);
    }
}

void StateMachine::distributeInitialResources_(uint8_t player, uint8_t vertex) {
    if (vertex >= VERTEX_COUNT) return;
    uint8_t adj[3];
    uint8_t cnt = tilesForVertex(vertex, adj, 3);
    for (uint8_t i = 0; i < cnt; ++i) {
        uint8_t t = adj[i];
        if (t >= TILE_COUNT) continue;
        uint8_t r_idx = biomeToResource(g_tile_state[t].biome);
        if (r_idx == NONE) continue;
        if (game::bankSupply((Res)r_idx) > 0) {
            game::drawFromBank((Res)r_idx, 1);
            game::addRes(player, (Res)r_idx, 1);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Tick — per-phase dispatch
// ──────────────────────────────────────────────────────────────────────────
void StateMachine::tick(uint32_t /*now_ms*/) {
    switch (game::phase()) {
        case GamePhase::LOBBY:             handleLobby_();            break;
        case GamePhase::BOARD_SETUP:       handleBoardSetup_();       break;
        case GamePhase::NUMBER_REVEAL:     handleNumberReveal_();     break;
        case GamePhase::INITIAL_PLACEMENT: handleInitialPlacement_(); break;
        case GamePhase::PLAYING:           handlePlaying_();          break;
        case GamePhase::ROBBER:            handleRobber_();           break;
        case GamePhase::DISCARD:           handleDiscard_();          break;
        case GamePhase::GAME_OVER:         handleGameOver_();         break;
    }
}

void StateMachine::handleLobby_() {
    uint8_t mask = game::connectedMask();
    if (mask != last_lobby_mask_) {
        last_lobby_mask_ = mask;
        pushEffect_(EffectKind::LOBBY_MASK_CHANGED, mask);
    }

    if (pending_start_game_ && game::numPlayers() >= MIN_PLAYERS) {
        pending_start_game_ = false;
        uint8_t ids[MAX_PLAYERS]; uint8_t cnt = 0;
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i)
            if (game::playerConnected(i)) ids[cnt++] = i;
        first_player_ = (cnt > 0) ? ids[core::rng::uniform(cnt)] : 0;
        board_setup_done_ = false;
        game::clearReady();
        game::initDevDeck();
        setPhase_(GamePhase::BOARD_SETUP);
    }
}

void StateMachine::handleBoardSetup_() {
    if (!board_setup_done_) {
        board_setup_done_ = true;
        randomizeBoardLayout();
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
            last_reveal_num_ = 0xFF;
            pushEffect_(EffectKind::REVEAL_NUMBER_CHANGED, 0);
            game::resetSetupRound(first_player_);
            setPhase_(GamePhase::INITIAL_PLACEMENT);
        }
    }
}

void StateMachine::handleInitialPlacement_() {
    if (pending_current_ == ActionKind::PLACE_DONE) {
        uint8_t cp     = game::currentPlayer();
        uint8_t needed = game::setupRound();   // 1 in round 1, 2 in round 2

        // Board is source of truth: count actual placed pieces.
        // The player must have placed at least `needed` settlements AND roads
        // before they are allowed to advance to the next setup turn.
        if (game::settlementCount(cp) < needed || game::roadCount(cp) < needed) {
            pending_current_ = ActionKind::NONE;
            emitReject_(cp, RejectReason::PLACEMENT_INCOMPLETE);
            return;
        }

        pending_current_ = ActionKind::NONE;
        // If this was the second-round placement, distribute starting resources.
        if (game::setupRound() == 2 && last_initial_vertex_ != NO_PLAYER) {
            distributeInitialResources_(game::currentPlayer(), last_initial_vertex_);
            last_initial_vertex_ = NO_PLAYER;
        }
        if (!game::advanceSetupTurn()) {
            game::setCurrentPlayer(first_player_);
            game::clearDice();
            recomputeAndEmitVp_();
            setPhase_(GamePhase::PLAYING);
        } else {
            pushEffect_(EffectKind::TURN_ADVANCED, game::currentPlayer());
        }
    }
}

void StateMachine::handlePlaying_() {
    if (pending_current_ == ActionKind::ROLL_DICE && !game::hasRolled()) {
        pending_current_ = ActionKind::NONE;
        game::rollDice();
        uint8_t d1 = game::lastDie1();
        uint8_t d2 = game::lastDie2();
        uint8_t total = (uint8_t)(d1 + d2);
        bool seven = (total == ROBBER_ROLL);
        pushEffect_(EffectKind::DICE_ROLLED, d1, d2, seven ? 1 : 0);
        if (!seven) {
            uint8_t dealt = game::distributeResources(total);
            pushEffect_(EffectKind::RESOURCES_DISTRIBUTED, total, dealt);
        } else {
            enterRobberOrDiscard_();
            return;
        }
    }

    if (pending_current_ == ActionKind::END_TURN && game::hasRolled()) {
        pending_current_   = ActionKind::NONE;
        pending_city_mask_ = 0;
        // Check winner BEFORE advancing turn — Catan rule: you can only win
        // on your own turn, and that's the turn that just ended.
        recomputeAndEmitVp_();
        if (game::phase() == GamePhase::GAME_OVER) return;
        game::clearLastDistribution();
        game::clearPendingTrade();
        game::setFreeRoadsRemaining(game::currentPlayer(), 0);
        game::nextTurn();
        pushEffect_(EffectKind::TURN_ADVANCED, game::currentPlayer());
    }
}

void StateMachine::handleRobber_() {
    if (pending_current_ == ActionKind::SKIP_ROBBER) {
        pending_current_ = ActionKind::NONE;
        // Only valid if no eligible targets remain.
        if (game::stealEligibleMask() == 0) {
            setPhase_(GamePhase::PLAYING);
        } else {
            emitReject_(game::currentPlayer(), RejectReason::WRONG_PHASE);
        }
    }
}

void StateMachine::handleDiscard_() {
    // Discards are processed immediately via onDiscard_; nothing to tick.
}

void StateMachine::handleGameOver_() {
    pending_current_     = ActionKind::NONE;
    pending_start_game_  = false;
    pending_next_number_ = false;
}

}  // namespace core
