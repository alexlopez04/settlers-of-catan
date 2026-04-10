#!/usr/bin/env python3
"""
generate_topology.py — Generate Catan board topology tables for firmware.

Produces the C++ topology arrays (tiles, vertices, edges, ports) using axial
hex coordinates for a standard 19-tile Catan board.

Usage:
    python3 tools/generate_topology.py

The output is formatted for direct inclusion in board_topology.cpp.
"""

import math
from collections import defaultdict

SQRT3 = math.sqrt(3.0)
NONE = 0xFF

# ─── Standard 19-tile Catan layout (axial coordinates: q, r) ────────────────
# Ring 0 (center), Ring 1 (CW from E), Ring 2 (CW from E)
TILES = [
    # Ring 0
    ( 0,  0),
    # Ring 1
    ( 1,  0), ( 0,  1), (-1,  1), (-1,  0), ( 0, -1), ( 1, -1),
    # Ring 2
    ( 2,  0), ( 1,  1), ( 0,  2), (-1,  2), (-2,  2), (-2,  1),
    (-2,  0), (-1, -1), ( 0, -2), ( 1, -2), ( 2, -2), ( 2, -1),
]


def axial_to_pixel(q, r):
    """Pointy-top hex: axial → Cartesian center."""
    x = SQRT3 * q + (SQRT3 / 2.0) * r
    y = 1.5 * r
    return (x, y)


def corner_offset(i):
    """Offset of corner i from hex center (pointy-top, CW from top).
    0=N  1=NE  2=SE  3=S  4=SW  5=NW"""
    angles = [90, 30, -30, -90, -150, 150]
    a = math.radians(angles[i])
    return (math.cos(a), math.sin(a))


def poskey(pos):
    """Hashable rounded position for merging coincident points."""
    return (round(pos[0], 4), round(pos[1], 4))


def walk_perimeter(coastal_eids, eid_pair):
    """Walk the coastal edges in order around the board perimeter."""
    coast_v_edges = defaultdict(list)
    for eid in coastal_eids:
        v1, v2 = eid_pair[eid]
        coast_v_edges[v1].append(eid)
        coast_v_edges[v2].append(eid)

    perimeter = []
    visited = set()
    current_eid = coastal_eids[0]
    current_v = eid_pair[current_eid][0]

    while True:
        visited.add(current_eid)
        perimeter.append(current_eid)
        v1, v2 = eid_pair[current_eid]
        next_v = v2 if current_v == v1 else v1
        found = False
        for cand in coast_v_edges[next_v]:
            if cand not in visited:
                current_eid = cand
                current_v = next_v
                found = True
                break
        if not found:
            break
    return perimeter


def generate():
    num_tiles = len(TILES)

    # ── Merge corners into unique vertices ───────────────────────────────
    pos_to_vid = {}
    vid_pos = []
    tile_vids = []

    for tid, (q, r) in enumerate(TILES):
        cx, cy = axial_to_pixel(q, r)
        vids = []
        for ci in range(6):
            dx, dy = corner_offset(ci)
            p = (cx + dx, cy + dy)
            k = poskey(p)
            if k not in pos_to_vid:
                pos_to_vid[k] = len(vid_pos)
                vid_pos.append(p)
            vids.append(pos_to_vid[k])
        tile_vids.append(vids)

    num_vertices = len(vid_pos)

    # ── Merge edge pairs into unique edges ───────────────────────────────
    epair_to_eid = {}
    eid_pair = []
    tile_eids = []

    for tid, vids in enumerate(tile_vids):
        eids = []
        for ci in range(6):
            v1, v2 = vids[ci], vids[(ci + 1) % 6]
            pair = (min(v1, v2), max(v1, v2))
            if pair not in epair_to_eid:
                epair_to_eid[pair] = len(eid_pair)
                eid_pair.append(pair)
            eids.append(epair_to_eid[pair])
        tile_eids.append(eids)

    num_edges = len(eid_pair)

    # ── Build adjacency maps ─────────────────────────────────────────────
    v_tiles = defaultdict(list)
    for tid, vids in enumerate(tile_vids):
        for vid in vids:
            if tid not in v_tiles[vid]:
                v_tiles[vid].append(tid)

    v_edges = defaultdict(list)
    for eid, (v1, v2) in enumerate(eid_pair):
        v_edges[v1].append(eid)
        v_edges[v2].append(eid)

    e_tiles = defaultdict(list)
    for tid, eids in enumerate(tile_eids):
        for eid in eids:
            if tid not in e_tiles[eid]:
                e_tiles[eid].append(tid)

    t_neighbors = []
    for tid in range(num_tiles):
        nbrs = set()
        for eid in tile_eids[tid]:
            for other in e_tiles[eid]:
                if other != tid:
                    nbrs.add(other)
        t_neighbors.append(sorted(nbrs))

    # ── Coastal identification ───────────────────────────────────────────
    coastal_eids = sorted(eid for eid in range(num_edges) if len(e_tiles[eid]) == 1)
    coastal_vids = sorted(vid for vid in range(num_vertices) if len(v_tiles[vid]) < 3)

    # ── Perimeter walk for port placement ────────────────────────────────
    perimeter = walk_perimeter(coastal_eids, eid_pair)

    # Pick 9 port edges roughly evenly spaced around the perimeter.
    # Standard Catan spaces them on every ~3rd coastal edge.
    n_coast = len(perimeter)
    step = n_coast / 9.0
    port_eids = []
    for i in range(9):
        idx = int(round(i * step)) % n_coast
        port_eids.append(perimeter[idx])

    port_types = [
        "PortType::GENERIC_3_1",
        "PortType::LUMBER_2_1",
        "PortType::GENERIC_3_1",
        "PortType::WOOL_2_1",
        "PortType::GENERIC_3_1",
        "PortType::GRAIN_2_1",
        "PortType::BRICK_2_1",
        "PortType::GENERIC_3_1",
        "PortType::ORE_2_1",
    ]

    # ── Output ───────────────────────────────────────────────────────────
    print(f"// ┌─────────────────────────────────────────────────────────────────┐")
    print(f"// │  AUTO-GENERATED by tools/generate_topology.py                   │")
    print(f"// │  Tiles: {num_tiles}   Vertices: {num_vertices}   Edges: {num_edges}   Ports: {len(port_eids)}          │")
    print(f"// │  Coastal edges: {len(coastal_eids)}   Coastal vertices: {len(coastal_vids)}              │")
    print(f"// └─────────────────────────────────────────────────────────────────┘")
    print()

    # ── TILE_TOPO ────────────────────────────────────────────────────────
    print(f"//   id   (q, r)       vertices[6] (CW from N)       edges[6] (CW from N)           neighbors (up to 6)")
    print(f"const TileDef TILE_TOPO[TILE_COUNT] = {{")
    for tid in range(num_tiles):
        q, r = TILES[tid]
        vs = tile_vids[tid]
        es = tile_eids[tid]
        ns = (t_neighbors[tid] + [NONE]*6)[:6]

        vs_s = "{" + ",".join(f"{v:3d}" for v in vs) + "}"
        es_s = "{" + ",".join(f"{e:3d}" for e in es) + "}"
        ns_s = "{" + ",".join(f"NONE" if n == NONE else f"{n:4d}" for n in ns) + "}"

        print(f"    /* T{tid:02d} */ {{ {tid:2d}, {{{q:2d},{r:2d}}}, {vs_s}, {es_s}, {ns_s} }},")
    print("};")
    print()

    # ── VERTEX_TOPO ──────────────────────────────────────────────────────
    print(f"//   id    tiles (up to 3)     edges (up to 3)")
    print(f"const VertexDef VERTEX_TOPO[VERTEX_COUNT] = {{")
    for vid in range(num_vertices):
        ts = (v_tiles[vid] + [NONE]*3)[:3]
        es = (sorted(v_edges[vid]) + [NONE]*3)[:3]

        ts_s = "{" + ",".join(f"NONE" if t == NONE else f"{t:4d}" for t in ts) + "}"
        es_s = "{" + ",".join(f"NONE" if e == NONE else f"{e:4d}" for e in es) + "}"

        print(f"    /* V{vid:02d} */ {{ {vid:2d}, {ts_s}, {es_s} }},")
    print("};")
    print()

    # ── EDGE_TOPO ────────────────────────────────────────────────────────
    print(f"//   id   vertices      tiles (up to 2)")
    print(f"const EdgeDef EDGE_TOPO[EDGE_COUNT] = {{")
    for eid in range(num_edges):
        v1, v2 = eid_pair[eid]
        ts = (e_tiles[eid] + [NONE]*2)[:2]

        ts_s = "{" + ",".join(f"NONE" if t == NONE else f"{t:4d}" for t in ts) + "}"

        print(f"    /* E{eid:02d} */ {{ {eid:2d}, {{{v1:3d},{v2:3d}}}, {ts_s} }},")
    print("};")
    print()

    # ── PORT_TOPO ────────────────────────────────────────────────────────
    print(f"//   id   type                    vertices")
    print(f"const PortDef PORT_TOPO[PORT_COUNT] = {{")
    for pid, eid in enumerate(port_eids):
        v1, v2 = eid_pair[eid]
        print(f"    /* P{pid} */ {{ {pid}, {port_types[pid]:24s}, {{{v1:3d},{v2:3d}}} }},")
    print("};")

    # ── Summary ──────────────────────────────────────────────────────────
    print()
    print(f"// Coastal vertices ({len(coastal_vids)}): ", end="")
    print(", ".join(str(v) for v in coastal_vids))
    print(f"// Coastal edges ({len(coastal_eids)}): ", end="")
    print(", ".join(str(e) for e in coastal_eids))
    print(f"// Perimeter walk ({len(perimeter)} edges): ", end="")
    print(", ".join(str(e) for e in perimeter))


if __name__ == "__main__":
    generate()
