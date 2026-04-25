"""
Board topology data for a standard 19-tile Settlers of Catan board.

Mirrors firmware/board/src/board_topology.cpp and app/src/constants/board-topology.ts.
All IDs, orderings, and adjacency tables are identical to the firmware.
"""

from typing import List, Tuple

TILE_COUNT   = 19
VERTEX_COUNT = 54
EDGE_COUNT   = 72

# Axial (q, r) hex coordinates for each tile, index = tile ID (T00..T18).
TILE_HEX_COORDS: List[Tuple[int, int]] = [
    ( 0,  0),  # T00 center
    ( 1,  0),  # T01
    ( 0,  1),  # T02
    (-1,  1),  # T03
    (-1,  0),  # T04
    ( 0, -1),  # T05
    ( 1, -1),  # T06
    ( 2,  0),  # T07
    ( 1,  1),  # T08
    ( 0,  2),  # T09
    (-1,  2),  # T10
    (-2,  2),  # T11
    (-2,  1),  # T12
    (-2,  0),  # T13
    (-1, -1),  # T14
    ( 0, -2),  # T15
    ( 1, -2),  # T16
    ( 2, -2),  # T17
    ( 2, -1),  # T18
]

# For each tile, the 6 vertex IDs in position order [S, SE, NE, N, NW, SW].
# Matches firmware TILE_TOPO vertices[] and board-topology.ts TILE_VERTICES.
TILE_VERTICES: List[List[int]] = [
    [ 0,  1,  2,  3,  4,  5],  # T00
    [ 6,  7,  8,  9,  2,  1],  # T01
    [10, 11,  6,  1,  0, 12],  # T02
    [13, 12,  0,  5, 14, 15],  # T03
    [14,  5,  4, 16, 17, 18],  # T04
    [ 4,  3, 19, 20, 21, 16],  # T05
    [ 2,  9, 22, 23, 19,  3],  # T06
    [24, 25, 26, 27,  8,  7],  # T07
    [28, 29, 24,  7,  6, 11],  # T08
    [30, 31, 28, 11, 10, 32],  # T09
    [33, 32, 10, 12, 13, 34],  # T10
    [35, 34, 13, 15, 36, 37],  # T11
    [36, 15, 14, 18, 38, 39],  # T12
    [38, 18, 17, 40, 41, 42],  # T13
    [17, 16, 21, 43, 44, 40],  # T14
    [21, 20, 45, 46, 47, 43],  # T15
    [19, 23, 48, 49, 45, 20],  # T16
    [22, 50, 51, 52, 48, 23],  # T17
    [ 8, 27, 53, 50, 22,  9],  # T18
]

# For each edge (E00..E71), the two vertex IDs it connects.
# Matches firmware EDGE_TOPO and board-topology.ts EDGE_VERTICES.
EDGE_VERTICES: List[Tuple[int, int]] = [
    ( 0,  1), ( 1,  2), ( 2,  3), ( 3,  4), ( 4,  5), ( 0,  5),   # E00–E05
    ( 6,  7), ( 7,  8), ( 8,  9), ( 2,  9), ( 1,  6),             # E06–E10
    (10, 11), ( 6, 11), ( 0, 12), (10, 12), (12, 13),             # E11–E15
    ( 5, 14), (14, 15), (13, 15),                                  # E16–E18
    ( 4, 16), (16, 17), (17, 18), (14, 18),                       # E19–E22
    ( 3, 19), (19, 20), (20, 21), (16, 21),                       # E23–E26
    ( 9, 22), (22, 23), (19, 23),                                  # E27–E29
    (24, 25), (25, 26), (26, 27), ( 8, 27), ( 7, 24),             # E30–E34
    (28, 29), (24, 29), (11, 28),                                  # E35–E37
    (30, 31), (28, 31), (10, 32), (30, 32),                       # E38–E41
    (32, 33), (13, 34), (33, 34),                                  # E42–E44
    (34, 35), (15, 36), (36, 37), (35, 37),                       # E45–E48
    (18, 38), (38, 39), (36, 39),                                  # E49–E51
    (17, 40), (40, 41), (41, 42), (38, 42),                       # E52–E55
    (21, 43), (43, 44), (40, 44),                                  # E56–E58
    (20, 45), (45, 46), (46, 47), (43, 47),                       # E59–E62
    (23, 48), (48, 49), (45, 49),                                  # E63–E65
    (22, 50), (50, 51), (51, 52), (48, 52),                       # E66–E69
    (27, 53), (50, 53),                                            # E70–E71
]

# Validate table sizes at import time.
assert len(TILE_HEX_COORDS) == TILE_COUNT
assert len(TILE_VERTICES)   == TILE_COUNT
assert len(EDGE_VERTICES)   == EDGE_COUNT
