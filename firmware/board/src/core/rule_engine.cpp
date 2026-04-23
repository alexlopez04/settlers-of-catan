// =============================================================================
// core/rule_engine.cpp — Pure placement validation.
//
// Reads game state via the `game::` namespace and board topology via
// VERTEX_TOPO / EDGE_TOPO. Uses no Arduino / hardware APIs.
// =============================================================================

#include "core/rule_engine.h"
#include "game_state.h"
#include "board_topology.h"
#include "config.h"

namespace core { namespace rules {

namespace {

// Return true if the vertex at `v` has a settlement or city owner.
bool vertexOccupied(uint8_t v) {
    if (v >= VERTEX_COUNT) return false;
    return game::vertexState(v).owner != NO_PLAYER;
}

// Distance-2 rule: no adjacent vertex (reachable via a single edge) may
// already hold a settlement or city.
bool hasAdjacentSettlement(uint8_t v) {
    if (v >= VERTEX_COUNT) return false;
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        const EdgeDef& ed = EDGE_TOPO[e];
        // The adjacent vertex is whichever endpoint isn't `v`.
        uint8_t other = (ed.vertices[0] == v) ? ed.vertices[1] : ed.vertices[0];
        if (other >= VERTEX_COUNT) continue;
        if (vertexOccupied(other)) return true;
    }
    return false;
}

// Does the player own a piece (settlement/city) or a road incident to
// vertex `v`? Used for road connectivity during play.
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

// For an initial-phase road, does vertex `v` hold a settlement of `player`
// that has NO player-owned road incident to it yet? That's the "just placed"
// settlement the initial road must extend from.
bool isFreshInitialSettlement(uint8_t v, uint8_t player) {
    if (v >= VERTEX_COUNT) return false;
    if (game::vertexState(v).owner != player) return false;
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        if (game::edgeState(e).owner == player) return false;  // already has a road
    }
    return true;
}

}  // anonymous namespace

RejectReason validateSettlement(uint8_t vertex, uint8_t player, bool initial) {
    if (vertex >= VERTEX_COUNT)   return RejectReason::INVALID_INDEX;
    if (player >= MAX_PLAYERS)    return RejectReason::INVALID_INDEX;
    if (vertexOccupied(vertex))   return RejectReason::VERTEX_OCCUPIED;
    if (hasAdjacentSettlement(vertex)) return RejectReason::TOO_CLOSE_TO_SETTLEMENT;

    if (!initial) {
        // Regular play: the vertex must be connected to a road the player owns.
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
    if (vertex >= VERTEX_COUNT)  return RejectReason::INVALID_INDEX;
    if (player >= MAX_PLAYERS)   return RejectReason::INVALID_INDEX;
    const VertexState& vs = game::vertexState(vertex);
    if (vs.owner != player) return RejectReason::NOT_MY_SETTLEMENT;
    if (vs.is_city)         return RejectReason::VERTEX_OCCUPIED;
    return RejectReason::NONE;
}

RejectReason validateRoad(uint8_t edge, uint8_t player, bool initial) {
    if (edge >= EDGE_COUNT)    return RejectReason::INVALID_INDEX;
    if (player >= MAX_PLAYERS) return RejectReason::INVALID_INDEX;
    if (game::edgeState(edge).owner != NO_PLAYER) return RejectReason::ROAD_OCCUPIED;

    const EdgeDef& ed = EDGE_TOPO[edge];
    const uint8_t va = ed.vertices[0];
    const uint8_t vb = ed.vertices[1];

    if (initial) {
        // Must touch a freshly-placed settlement of `player`.
        if (isFreshInitialSettlement(va, player)) return RejectReason::NONE;
        if (isFreshInitialSettlement(vb, player)) return RejectReason::NONE;
        return RejectReason::ROAD_NOT_CONNECTED;
    }

    // Regular play: must touch any of the player's pieces or roads.
    if (playerTouchesVertex(va, player)) return RejectReason::NONE;
    if (playerTouchesVertex(vb, player)) return RejectReason::NONE;
    return RejectReason::ROAD_NOT_CONNECTED;
}

RejectReason validateRobberMove(uint8_t tile) {
    if (tile >= TILE_COUNT) return RejectReason::INVALID_INDEX;
    if (tile == game::robberTile()) return RejectReason::ROBBER_SAME_TILE;
    return RejectReason::NONE;
}

}}  // namespace core::rules
