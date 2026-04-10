#pragma once
// =============================================================================
// board_topology.h — Pure Catan board topology (graph structure).
//
// Contains the static planar-graph definition of a standard 19-tile board:
//   • TileDef  — hex coordinate, surrounding vertex & edge IDs, neighbor tiles
//   • VertexDef — adjacent tiles and connected edges
//   • EdgeDef  — the two endpoint vertices and bordering tiles
//   • PortDef  — port type and the two coastal vertices
//
// Runtime state (biome assignments, number tokens) is stored separately in
// TileState and managed by randomizeBoardLayout().
//
// Regenerate the topology tables with:  python3 tools/generate_topology.py
// =============================================================================

#include <stdint.h>
#include "config.h"
#include "board_types.h"

// ── Static topology structs (const, ROM-safe) ───────────────────────────────

struct TileDef {
    uint8_t  id;
    HexCoord coord;          // Axial (q, r)
    uint8_t  vertices[6];    // Vertex IDs, clockwise from North
    uint8_t  edges[6];       // Edge IDs, clockwise from North
    uint8_t  neighbors[6];   // Adjacent tile IDs (NONE if border)
};

struct VertexDef {
    uint8_t id;
    uint8_t tiles[3];        // Adjacent tile IDs (NONE if < 3)
    uint8_t edges[3];        // Connected edge IDs (NONE if < 3)
};

struct EdgeDef {
    uint8_t id;
    uint8_t vertices[2];     // The two endpoint vertex IDs
    uint8_t tiles[2];        // Bordering tile IDs (NONE if coastal)
};

struct PortDef {
    uint8_t  id;
    PortType type;
    uint8_t  vertices[2];    // The two coastal vertex IDs
};

// ── Runtime tile state (mutable — assigned by randomization) ────────────────

struct TileState {
    Biome   biome;
    uint8_t number;          // 2–12 (0 for desert)
};

// ── Topology tables (defined in board_topology.cpp) ─────────────────────────

extern const TileDef   TILE_TOPO[TILE_COUNT];
extern const VertexDef VERTEX_TOPO[VERTEX_COUNT];
extern const EdgeDef   EDGE_TOPO[EDGE_COUNT];
extern const PortDef   PORT_TOPO[PORT_COUNT];

// ── Runtime tile state ──────────────────────────────────────────────────────

extern TileState g_tile_state[TILE_COUNT];

// ── Adjacency helpers ───────────────────────────────────────────────────────

// Tiles touching a vertex (up to 3).  Returns count written to `out`.
uint8_t tilesForVertex(uint8_t vertex_id, uint8_t out[], uint8_t max_out);

// Tiles bordering an edge (up to 2).  Returns count written to `out`.
uint8_t tilesForEdge(uint8_t edge_id, uint8_t out[], uint8_t max_out);

// ── Randomization ───────────────────────────────────────────────────────────

// Assigns biomes and number tokens according to standard Catan rules.
void randomizeBoardLayout();
