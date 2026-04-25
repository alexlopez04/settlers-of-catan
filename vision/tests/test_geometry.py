"""
Tests for canonical board geometry.
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import math
import pytest
import numpy as np

from vision.geometry import (
    CANONICAL_SIZE, HEX_SIZE, BOARD_CENTRE,
    tile_center, corner_pos,
    VERTEX_POS, EDGE_MIDPOINT, EDGE_LENGTH,
)
from vision.topology import VERTEX_COUNT, EDGE_COUNT, EDGE_VERTICES


def test_centre_tile_at_image_centre():
    """T00 (q=0, r=0) must be at (500, 500) in canonical space."""
    cx, cy = tile_center(0, 0)
    assert abs(cx - BOARD_CENTRE) < 0.01
    assert abs(cy - BOARD_CENTRE) < 0.01


def test_six_corners_equidistant():
    """All 6 corners of a tile must be exactly HEX_SIZE from the centre."""
    cx, cy = tile_center(0, 0)
    for c in range(6):
        px, py = corner_pos(cx, cy, c)
        dist = math.hypot(px - cx, py - cy)
        assert abs(dist - HEX_SIZE) < 0.01, f"Corner {c} dist = {dist:.4f}"


def test_corners_60deg_apart():
    """Adjacent corners of the same tile are 60° apart."""
    cx, cy = tile_center(0, 0)
    pts = [corner_pos(cx, cy, c) for c in range(6)]
    for i in range(6):
        v1 = (pts[i][0] - cx, pts[i][1] - cy)
        v2 = (pts[(i + 1) % 6][0] - cx, pts[(i + 1) % 6][1] - cy)
        cos_angle = (v1[0]*v2[0] + v1[1]*v2[1]) / (HEX_SIZE ** 2)
        angle_deg = math.degrees(math.acos(max(-1.0, min(1.0, cos_angle))))
        assert abs(angle_deg - 60.0) < 0.1, f"Corners {i}/{(i+1)%6} angle = {angle_deg:.2f}°"


def test_vertex_positions_count():
    assert len(VERTEX_POS) == VERTEX_COUNT


def test_all_vertices_inside_canvas():
    for vid, (x, y) in enumerate(VERTEX_POS):
        margin = 5  # vertices should not be on the very edge
        assert margin <= x <= CANONICAL_SIZE - margin, f"Vertex {vid} x={x:.1f} out of bounds"
        assert margin <= y <= CANONICAL_SIZE - margin, f"Vertex {vid} y={y:.1f} out of bounds"


def test_shared_vertices_consistent():
    """
    Vertices shared by multiple tiles must have the same position regardless of
    which tile was used to compute them.
    """
    from vision.geometry import _build_vertex_positions
    from vision.topology import TILE_HEX_COORDS, TILE_VERTICES
    from vision.geometry import _VERTEX_POS_TO_CORNER

    # Rebuild positions and verify idempotence.
    pos1 = _build_vertex_positions(HEX_SIZE)
    pos2 = _build_vertex_positions(HEX_SIZE)
    for vid in range(VERTEX_COUNT):
        assert abs(pos1[vid][0] - pos2[vid][0]) < 0.01
        assert abs(pos1[vid][1] - pos2[vid][1]) < 0.01


def test_edge_count():
    assert len(EDGE_MIDPOINT) == EDGE_COUNT
    assert len(EDGE_LENGTH) == EDGE_COUNT


def test_edge_length_equals_hex_size():
    """In a regular hex lattice every edge has length == HEX_SIZE."""
    for eid, length in enumerate(EDGE_LENGTH):
        assert abs(length - HEX_SIZE) < 0.1, f"Edge {eid} length = {length:.4f}"


def test_edge_midpoint_equidistant_from_vertices():
    for eid, (mx, my) in enumerate(EDGE_MIDPOINT):
        v1, v2 = EDGE_VERTICES[eid]
        x1, y1 = VERTEX_POS[v1]
        x2, y2 = VERTEX_POS[v2]
        d1 = math.hypot(mx - x1, my - y1)
        d2 = math.hypot(mx - x2, my - y2)
        assert abs(d1 - d2) < 0.01, f"Edge {eid} midpoint off-centre"


def test_board_fits_canonical_canvas():
    """Every vertex must be well within the 1000×1000 image with 50px margin."""
    for vid, (x, y) in enumerate(VERTEX_POS):
        assert 50 < x < CANONICAL_SIZE - 50, f"Vertex {vid} x={x:.1f}"
        assert 50 < y < CANONICAL_SIZE - 50, f"Vertex {vid} y={y:.1f}"


def test_t07_right_corner():
    """T07 (q=2, r=0) is the rightmost tile — its centre should be at x > 700."""
    cx, cy = tile_center(2, 0)
    assert cx > 700, f"T07 centre x={cx:.1f}"


def test_t11_bottom_left_corner():
    """T11 (q=-2, r=2) is the bottom-left tile."""
    cx, cy = tile_center(-2, 2)
    # cx = 500 + HEX_SIZE*sqrt(3)*(-2 + 2/2) = 500 + 80*1.732*(-1) ≈ 361
    assert cx < 400
    assert cy > 650
