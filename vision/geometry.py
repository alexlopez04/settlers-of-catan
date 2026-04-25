"""
Canonical board geometry: compute 2D pixel positions for all 54 vertices
and 72 edge midpoints in a top-down canonical image.

Coordinate system: pointy-top axial hex (q, r).
  Tile centre in canonical space:
    x = CENTRE + HEX_SIZE * sqrt(3) * (q + r/2)
    y = CENTRE + HEX_SIZE * 1.5 * r
  Corner offset from centre (CW from N, 0=N … 5=NW):
    angle = -90° + corner * 60°
    dx = HEX_SIZE * cos(angle)
    dy = HEX_SIZE * sin(angle)

TILE_VERTICES stores positions in order [S, SE, NE, N, NW, SW] which maps to
corner indices [3, 2, 1, 0, 5, 4] in the formula above.
"""

import math
from typing import List, Tuple

from .topology import TILE_COUNT, VERTEX_COUNT, EDGE_COUNT, TILE_HEX_COORDS, TILE_VERTICES, EDGE_VERTICES

# ── Constants ──────────────────────────────────────────────────────────────

CANONICAL_SIZE = 1000        # pixels (square image)
HEX_SIZE       = 80.0        # pixels: tile centre-to-vertex radius
BOARD_CENTRE   = CANONICAL_SIZE / 2  # 500 px

# TILE_VERTICES position index → firmware corner index (0=N CW)
_VERTEX_POS_TO_CORNER: List[int] = [3, 2, 1, 0, 5, 4]


# ── Core geometry helpers ──────────────────────────────────────────────────

def tile_center(q: int, r: int, hex_size: float = HEX_SIZE) -> Tuple[float, float]:
    """Axial (q, r) → pixel centre in canonical space."""
    x = BOARD_CENTRE + hex_size * math.sqrt(3) * (q + r / 2.0)
    y = BOARD_CENTRE + hex_size * 1.5 * r
    return x, y


def corner_pos(cx: float, cy: float, corner: int, hex_size: float = HEX_SIZE) -> Tuple[float, float]:
    """
    Pixel position of a hex corner from tile centre.
    corner: 0=N, 1=NE, 2=SE, 3=S, 4=SW, 5=NW (CW, y-axis down).
    """
    angle_rad = math.radians(-90.0 + corner * 60.0)
    return (
        cx + hex_size * math.cos(angle_rad),
        cy + hex_size * math.sin(angle_rad),
    )


# ── Derived position tables (computed once at module load) ─────────────────

def _build_vertex_positions(hex_size: float = HEX_SIZE) -> List[Tuple[float, float]]:
    """Return canonical (x, y) for each of the 54 vertices."""
    out: List[Tuple[float, float]] = [(0.0, 0.0)] * VERTEX_COUNT
    assigned = [False] * VERTEX_COUNT

    for tile_id, (q, r) in enumerate(TILE_HEX_COORDS):
        cx, cy = tile_center(q, r, hex_size)
        for pos_idx, vertex_id in enumerate(TILE_VERTICES[tile_id]):
            corner = _VERTEX_POS_TO_CORNER[pos_idx]
            px, py = corner_pos(cx, cy, corner, hex_size)
            out[vertex_id] = (px, py)
            assigned[vertex_id] = True

    if not all(assigned):
        missing = [i for i, a in enumerate(assigned) if not a]
        raise RuntimeError(f"Vertices never assigned a position: {missing}")
    return out


def _build_edge_midpoints(
    vpos: List[Tuple[float, float]],
) -> List[Tuple[float, float]]:
    """Return the midpoint (x, y) for each of the 72 edges."""
    out = []
    for v1, v2 in EDGE_VERTICES:
        x1, y1 = vpos[v1]
        x2, y2 = vpos[v2]
        out.append(((x1 + x2) / 2.0, (y1 + y2) / 2.0))
    return out


def _build_edge_angles(
    vpos: List[Tuple[float, float]],
) -> List[float]:
    """Return orientation angle (degrees, from +x axis) for each edge."""
    return [
        math.degrees(math.atan2(vpos[v2][1] - vpos[v1][1], vpos[v2][0] - vpos[v1][0]))
        for v1, v2 in EDGE_VERTICES
    ]


def _build_edge_lengths(
    vpos: List[Tuple[float, float]],
) -> List[float]:
    return [
        math.hypot(vpos[v2][0] - vpos[v1][0], vpos[v2][1] - vpos[v1][1])
        for v1, v2 in EDGE_VERTICES
    ]


# Pre-computed tables (default hex_size).
VERTEX_POS:    List[Tuple[float, float]] = _build_vertex_positions()
EDGE_MIDPOINT: List[Tuple[float, float]] = _build_edge_midpoints(VERTEX_POS)
EDGE_ANGLE:    List[float]               = _build_edge_angles(VERTEX_POS)
EDGE_LENGTH:   List[float]               = _build_edge_lengths(VERTEX_POS)
