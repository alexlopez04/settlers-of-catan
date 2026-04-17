#pragma once
// =============================================================================
// board_topology.h — Pure Catan board topology (graph structure).
// =============================================================================

#include <stdint.h>
#include "config.h"
#include "board_types.h"

struct TileDef {
    uint8_t  id;
    HexCoord coord;
    uint8_t  vertices[6];
    uint8_t  edges[6];
    uint8_t  neighbors[6];
};

struct VertexDef {
    uint8_t id;
    uint8_t tiles[3];
    uint8_t edges[3];
};

struct EdgeDef {
    uint8_t id;
    uint8_t vertices[2];
    uint8_t tiles[2];
};

struct PortDef {
    uint8_t  id;
    PortType type;
    uint8_t  vertices[2];
};

struct TileState {
    Biome   biome;
    uint8_t number;   // 2–12 (0 for desert)
};

extern const TileDef   TILE_TOPO[TILE_COUNT];
extern const VertexDef VERTEX_TOPO[VERTEX_COUNT];
extern const EdgeDef   EDGE_TOPO[EDGE_COUNT];
extern const PortDef   PORT_TOPO[PORT_COUNT];
extern TileState       g_tile_state[TILE_COUNT];

uint8_t tilesForVertex(uint8_t vertex_id, uint8_t out[], uint8_t max_out);
uint8_t tilesForEdge(uint8_t edge_id, uint8_t out[], uint8_t max_out);
void    randomizeBoardLayout();
