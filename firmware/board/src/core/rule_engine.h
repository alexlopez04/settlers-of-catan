#pragma once
// =============================================================================
// core/rule_engine.h — Pure placement validation for Catan pieces.
//
// The rule engine answers "is this placement legal?" given the current
// game state and board topology. It performs NO state mutation — the
// StateMachine calls it before deciding whether to commit a placement.
//
// Scope (per spec "Non-goals"): the engine enforces PLACEMENT validity
// only. It does not check resource costs, VP caps, or building limits —
// those are the player's / mobile app's responsibility.
// =============================================================================

#include <stdint.h>
#include "core/events.h"

namespace core { namespace rules {

// Validate a settlement placement.
//   `initial` = true during INITIAL_PLACEMENT (no road-connectivity check).
// Returns RejectReason::NONE if valid.
RejectReason validateSettlement(uint8_t vertex, uint8_t player, bool initial);

// Validate upgrading a settlement owned by `player` at `vertex` into a city.
RejectReason validateCity(uint8_t vertex, uint8_t player);

// Validate a road placement.
//   `initial` = true during INITIAL_PLACEMENT (must touch the settlement
//   just placed — approximated here as touching ANY settlement owned by
//   `player` with no road yet adjacent).
RejectReason validateRoad(uint8_t edge, uint8_t player, bool initial);

// Validate a robber move — fails if `tile` == current robber tile or out
// of range.
RejectReason validateRobberMove(uint8_t tile);

}}  // namespace core::rules
