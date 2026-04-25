"""
Tests for board topology data integrity.
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import pytest
from vision.topology import (
    TILE_COUNT, VERTEX_COUNT, EDGE_COUNT,
    TILE_HEX_COORDS, TILE_VERTICES, EDGE_VERTICES,
)


def test_tile_count():
    assert len(TILE_HEX_COORDS) == TILE_COUNT == 19


def test_vertex_count_from_edges():
    """Every vertex ID referenced by edges must be in [0, VERTEX_COUNT)."""
    referenced = set()
    for v1, v2 in EDGE_VERTICES:
        referenced.add(v1)
        referenced.add(v2)
    assert max(referenced) < VERTEX_COUNT
    assert min(referenced) >= 0


def test_edge_count():
    assert len(EDGE_VERTICES) == EDGE_COUNT == 72


def test_tile_vertices_each_has_six():
    for tile_id, verts in enumerate(TILE_VERTICES):
        assert len(verts) == 6, f"T{tile_id:02d} has {len(verts)} vertices"


def test_all_vertex_ids_referenced():
    """Each vertex ID 0-53 appears in at least one tile's vertex list."""
    seen = set()
    for verts in TILE_VERTICES:
        seen.update(verts)
    for vid in range(VERTEX_COUNT):
        assert vid in seen, f"Vertex {vid} never appears in TILE_VERTICES"


def test_edge_vertex_ids_valid():
    for eid, (v1, v2) in enumerate(EDGE_VERTICES):
        assert v1 != v2, f"Edge {eid} connects vertex to itself"
        assert 0 <= v1 < VERTEX_COUNT
        assert 0 <= v2 < VERTEX_COUNT


def test_no_duplicate_edges():
    """No two edges share the same unordered pair of vertices."""
    pairs = [frozenset(e) for e in EDGE_VERTICES]
    assert len(pairs) == len(set(pairs)), "Duplicate edge pairs found"


def test_hex_coords_unique():
    coords = set(TILE_HEX_COORDS)
    assert len(coords) == TILE_COUNT, "Duplicate tile coordinates"


def test_tile_vertices_range():
    for tile_id, verts in enumerate(TILE_VERTICES):
        for v in verts:
            assert 0 <= v < VERTEX_COUNT, f"T{tile_id:02d} references invalid vertex {v}"
