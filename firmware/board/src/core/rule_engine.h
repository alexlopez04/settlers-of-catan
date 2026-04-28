#pragma once
// =============================================================================
// core/rule_engine.h — Pure validation primitives for Catan game actions.
//
// The rule engine answers questions of the form "given the current game
// state, is this action legal?" It performs NO state mutation — the
// StateMachine calls into it before deciding whether to commit.
// =============================================================================

#include <stdint.h>
#include "core/events.h"
#include "game_state.h"

namespace core { namespace rules {

// Validate a settlement placement.
//   `initial` = true during INITIAL_PLACEMENT (no road-connectivity check).
RejectReason validateSettlement(uint8_t vertex, uint8_t player, bool initial);

// Validate upgrading a settlement owned by `player` at `vertex` into a city.
RejectReason validateCity(uint8_t vertex, uint8_t player);

// Validate a road placement.
//   `initial` = true during INITIAL_PLACEMENT (must touch the player's
//   freshly-placed settlement that has no incident road yet).
RejectReason validateRoad(uint8_t edge, uint8_t player, bool initial);

// Validate a robber move — fails if `tile` == current robber tile or out of range.
RejectReason validateRobberMove(uint8_t tile);

// ── Purchases ──────────────────────────────────────────────────────────────
// `kind`: 1=road, 2=settlement, 3=city, 4=dev card.
// Validates piece-limit (where applicable) and resource cost, but does NOT
// commit the spend. Caller must spend separately.
RejectReason validatePurchase(uint8_t player, uint8_t kind);

// ── Discard ────────────────────────────────────────────────────────────────
// `counts`: the 5 resource counts the player wants to discard (LWGBO).
// Validates that the player is in the discard mask, that the counts sum
// to the required amount, and that the player owns those resources.
RejectReason validateDiscard(uint8_t player, const uint8_t counts[5]);

// ── Trades ─────────────────────────────────────────────────────────────────
// Bank trade rate for a given resource: 4 (default), 3 (generic 3:1 port),
// or 2 (specific 2:1 port). Computed from the player's settlements/cities
// adjacent to ports.
uint8_t bankTradeRate(uint8_t player, uint8_t resource);

// Validate a bank trade: player must hold give[] resources at correct rate
// for want[], and the bank must hold the wanted amounts.
RejectReason validateBankTrade(uint8_t player,
                               const uint8_t give[5],
                               const uint8_t want[5]);

// Validate opening a P2P trade: caller is current player, has rolled,
// owns the offered resources, and offer/want are non-zero & non-overlapping.
RejectReason validateTradeOffer(uint8_t player,
                                const uint8_t offer[5],
                                const uint8_t want[5]);

// Validate accepting the pending trade as `acceptor`: trade exists,
// acceptor is the target (or any opponent for open offer), acceptor has
// the wanted resources.
RejectReason validateTradeAccept(uint8_t acceptor);

}}  // namespace core::rules
