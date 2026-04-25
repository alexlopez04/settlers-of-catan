// =============================================================================
// core/rule_engine.cpp — Pure validation primitives.
// =============================================================================

#include "core/rule_engine.h"
#include "game_state.h"
#include "board_topology.h"
#include "config.h"
#include <string.h>

namespace core { namespace rules {

namespace {

bool vertexOccupied(uint8_t v) {
    if (v >= VERTEX_COUNT) return false;
    return game::vertexState(v).owner != NO_PLAYER;
}

bool hasAdjacentSettlement(uint8_t v) {
    if (v >= VERTEX_COUNT) return false;
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        const EdgeDef& ed = EDGE_TOPO[e];
        uint8_t other = (ed.vertices[0] == v) ? ed.vertices[1] : ed.vertices[0];
        if (other >= VERTEX_COUNT) continue;
        if (vertexOccupied(other)) return true;
    }
    return false;
}

bool playerTouchesVertex(uint8_t v, uint8_t player) {
    if (v >= VERTEX_COUNT) return false;
    if (game::vertexState(v).owner == player) return true;
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        if (game::edgeState(e).owner == player) return true;
    }
    return false;
}

bool isFreshInitialSettlement(uint8_t v, uint8_t player) {
    if (v >= VERTEX_COUNT) return false;
    if (game::vertexState(v).owner != player) return false;
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        if (game::edgeState(e).owner == player) return false;
    }
    return true;
}

uint8_t portResource(PortType pt) {
    switch (pt) {
        case PortType::LUMBER_2_1: return 0;
        case PortType::WOOL_2_1:   return 1;
        case PortType::GRAIN_2_1:  return 2;
        case PortType::BRICK_2_1:  return 3;
        case PortType::ORE_2_1:    return 4;
        default:                   return 0xFF;
    }
}

}  // anonymous

// ── Placement ──────────────────────────────────────────────────────────────
RejectReason validateSettlement(uint8_t vertex, uint8_t player, bool initial) {
    if (vertex >= VERTEX_COUNT)        return RejectReason::INVALID_INDEX;
    if (player >= MAX_PLAYERS)         return RejectReason::INVALID_INDEX;
    if (vertexOccupied(vertex))        return RejectReason::VERTEX_OCCUPIED;
    if (hasAdjacentSettlement(vertex)) return RejectReason::TOO_CLOSE_TO_SETTLEMENT;

    if (game::settlementCount(player) + game::cityCount(player) >= MAX_SETTLEMENTS_PER_PLAYER + MAX_CITIES_PER_PLAYER) {
        return RejectReason::PIECE_LIMIT_REACHED;
    }
    if (game::settlementCount(player) >= MAX_SETTLEMENTS_PER_PLAYER) {
        return RejectReason::PIECE_LIMIT_REACHED;
    }

    if (!initial) {
        const VertexDef& vd = VERTEX_TOPO[vertex];
        bool has_road = false;
        for (uint8_t i = 0; i < 3; ++i) {
            uint8_t e = vd.edges[i];
            if (e >= EDGE_COUNT) continue;
            if (game::edgeState(e).owner == player) { has_road = true; break; }
        }
        if (!has_road) return RejectReason::ROAD_NOT_CONNECTED;
    }
    return RejectReason::NONE;
}

RejectReason validateCity(uint8_t vertex, uint8_t player) {
    if (vertex >= VERTEX_COUNT) return RejectReason::INVALID_INDEX;
    if (player >= MAX_PLAYERS)  return RejectReason::INVALID_INDEX;
    const VertexState& vs = game::vertexState(vertex);
    if (vs.owner != player) return RejectReason::NOT_MY_SETTLEMENT;
    if (vs.is_city)         return RejectReason::VERTEX_OCCUPIED;
    if (game::cityCount(player) >= MAX_CITIES_PER_PLAYER)
        return RejectReason::PIECE_LIMIT_REACHED;
    return RejectReason::NONE;
}

RejectReason validateRoad(uint8_t edge, uint8_t player, bool initial) {
    if (edge >= EDGE_COUNT)    return RejectReason::INVALID_INDEX;
    if (player >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    if (game::edgeState(edge).owner != NO_PLAYER) return RejectReason::ROAD_OCCUPIED;
    if (game::roadCount(player) >= MAX_ROADS_PER_PLAYER)
        return RejectReason::PIECE_LIMIT_REACHED;

    const EdgeDef& ed = EDGE_TOPO[edge];
    const uint8_t va = ed.vertices[0];
    const uint8_t vb = ed.vertices[1];

    if (initial) {
        if (isFreshInitialSettlement(va, player)) return RejectReason::NONE;
        if (isFreshInitialSettlement(vb, player)) return RejectReason::NONE;
        return RejectReason::ROAD_NOT_CONNECTED;
    }

    if (playerTouchesVertex(va, player)) return RejectReason::NONE;
    if (playerTouchesVertex(vb, player)) return RejectReason::NONE;
    return RejectReason::ROAD_NOT_CONNECTED;
}

RejectReason validateRobberMove(uint8_t tile) {
    if (tile >= TILE_COUNT) return RejectReason::INVALID_INDEX;
    if (tile == game::robberTile()) return RejectReason::ROBBER_SAME_TILE;
    return RejectReason::NONE;
}

// ── Purchases ──────────────────────────────────────────────────────────────
RejectReason validatePurchase(uint8_t player, uint8_t kind) {
    if (player >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    const ResourceCost* cost = nullptr;
    switch (kind) {
        case 1: cost = &ROAD_COST; break;
        case 2: cost = &SETTLEMENT_COST; break;
        case 3: cost = &CITY_COST; break;
        case 4: cost = &DEV_CARD_COST; break;
        default: return RejectReason::INVALID_INDEX;
    }
    // Piece limits.
    if (kind == 1) {
        uint8_t total_roads = (uint8_t)(game::roadCount(player) + game::pendingRoadBuy(player));
        if (total_roads >= MAX_ROADS_PER_PLAYER) return RejectReason::PIECE_LIMIT_REACHED;
    } else if (kind == 2) {
        uint8_t total = (uint8_t)(game::settlementCount(player) + game::pendingSettlementBuy(player));
        if (total >= MAX_SETTLEMENTS_PER_PLAYER) return RejectReason::PIECE_LIMIT_REACHED;
    } else if (kind == 3) {
        uint8_t total = (uint8_t)(game::cityCount(player) + game::pendingCityBuy(player));
        if (total >= MAX_CITIES_PER_PLAYER) return RejectReason::PIECE_LIMIT_REACHED;
    } else if (kind == 4) {
        if (game::devDeckRemaining() == 0) return RejectReason::DEV_DECK_EMPTY;
    }
    if (!game::hasRes(player, *cost)) return RejectReason::INSUFFICIENT_RESOURCES;
    return RejectReason::NONE;
}

// ── Discard ────────────────────────────────────────────────────────────────
RejectReason validateDiscard(uint8_t player, const uint8_t counts[5]) {
    if (player >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    uint8_t mask = game::discardRequiredMask();
    if (!(mask & (1u << player))) return RejectReason::INVALID_DISCARD;

    uint16_t total = 0;
    for (uint8_t r = 0; r < 5; ++r) {
        total += counts[r];
        if (game::resCount(player, (Res)r) < counts[r])
            return RejectReason::INSUFFICIENT_RESOURCES;
    }
    if (total != game::discardRequiredCount(player))
        return RejectReason::INVALID_DISCARD;
    return RejectReason::NONE;
}

// ── Trade rate ─────────────────────────────────────────────────────────────
uint8_t bankTradeRate(uint8_t player, uint8_t resource) {
    if (player >= MAX_PLAYERS || resource >= 5) return 4;
    uint8_t best = 4;
    for (uint8_t pi = 0; pi < PORT_COUNT; ++pi) {
        const PortDef& pd = PORT_TOPO[pi];
        bool owns = false;
        for (uint8_t s = 0; s < 2; ++s) {
            uint8_t v = pd.vertices[s];
            if (v < VERTEX_COUNT && game::vertexState(v).owner == player) {
                owns = true; break;
            }
        }
        if (!owns) continue;
        if (pd.type == PortType::GENERIC_3_1) {
            if (best > 3) best = 3;
        } else {
            uint8_t pr = portResource(pd.type);
            if (pr == resource) {
                if (best > 2) best = 2;
            }
        }
    }
    return best;
}

RejectReason validateBankTrade(uint8_t player,
                               const uint8_t give[5],
                               const uint8_t want[5]) {
    if (player >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    uint8_t give_total = 0;
    uint8_t want_total = 0;
    for (uint8_t r = 0; r < 5; ++r) {
        give_total += give[r];
        want_total += want[r];
    }
    if (give_total == 0 || want_total == 0) return RejectReason::INVALID_TRADE;

    // For each resource being given, the count must be a multiple of the
    // player's bank trade rate for that resource. The expected received
    // count must match the sum of (give[r] / rate(r)).
    uint8_t expected = 0;
    for (uint8_t r = 0; r < 5; ++r) {
        if (give[r] == 0) continue;
        uint8_t rate = bankTradeRate(player, r);
        if (give[r] % rate != 0) return RejectReason::INVALID_TRADE;
        expected = (uint8_t)(expected + give[r] / rate);
        if (game::resCount(player, (Res)r) < give[r])
            return RejectReason::INSUFFICIENT_RESOURCES;
    }
    if (expected != want_total) return RejectReason::INVALID_TRADE;

    // Bank must have wanted resources.
    for (uint8_t r = 0; r < 5; ++r) {
        if (want[r] && game::bankSupply((Res)r) < want[r])
            return RejectReason::BANK_DEPLETED;
    }
    return RejectReason::NONE;
}

RejectReason validateTradeOffer(uint8_t player,
                                const uint8_t offer[5],
                                const uint8_t want[5]) {
    if (player >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    if (player != game::currentPlayer()) return RejectReason::OUT_OF_TURN;
    if (!game::hasRolled()) return RejectReason::WRONG_PHASE;
    uint8_t off_total = 0, want_total = 0;
    for (uint8_t r = 0; r < 5; ++r) {
        off_total  += offer[r];
        want_total += want[r];
        if (offer[r] && want[r]) return RejectReason::INVALID_TRADE;  // can't give & want same
        if (game::resCount(player, (Res)r) < offer[r])
            return RejectReason::INSUFFICIENT_RESOURCES;
    }
    if (off_total == 0 || want_total == 0) return RejectReason::INVALID_TRADE;
    return RejectReason::NONE;
}

RejectReason validateTradeAccept(uint8_t acceptor) {
    if (acceptor >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    if (!game::hasPendingTrade()) return RejectReason::NO_PENDING_TRADE;
    const auto& t = game::pendingTrade();
    if (acceptor == t.from) return RejectReason::INVALID_TRADE;
    if (t.to != NO_PLAYER && t.to != acceptor) return RejectReason::INVALID_TRADE;
    for (uint8_t r = 0; r < 5; ++r) {
        if (game::resCount(acceptor, (Res)r) < t.want[r])
            return RejectReason::INSUFFICIENT_RESOURCES;
    }
    return RejectReason::NONE;
}

}}  // namespace
