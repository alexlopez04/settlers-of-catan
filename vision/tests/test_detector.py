"""
Tests for the BoardDetector on canonical images (no alignment step).

All tests here use skip_alignment=True so they exercise only the piece
detection logic (vertex sampling + edge sampling + color classification).
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import pytest
import numpy as np

from vision.detector import BoardDetector, DetectorConfig, BoardState
from vision.topology import VERTEX_COUNT, EDGE_COUNT
from vision.tests.board_generator import (
    BoardSetup, SceneConfig, WarpParams, draw_canonical_board,
    apply_lighting, add_noise, make_test_image, PLAYER_COLORS_BGR,
)


# ── Fixture: detector that works on canonical images ──────────────────────

@pytest.fixture
def det() -> BoardDetector:
    return BoardDetector(DetectorConfig(skip_alignment=True))


# ── Helper ─────────────────────────────────────────────────────────────────

def _accuracy(state: BoardState, setup: BoardSetup) -> dict:
    """
    Compute per-class accuracy between detected state and ground truth.

    Returns dict with keys:
      vertex_correct, vertex_total
      edge_correct,   edge_total
      vertex_acc, edge_acc
    """
    v_correct = e_correct = 0

    for vid in range(VERTEX_COUNT):
        expected = setup.vertex_owners.get(vid)
        got = state.vertices.get(vid)
        if got == expected:
            v_correct += 1

    for eid in range(EDGE_COUNT):
        expected = setup.edge_owners.get(eid)
        got = state.edges.get(eid)
        if got == expected:
            e_correct += 1

    return {
        "vertex_correct": v_correct,
        "vertex_total":   VERTEX_COUNT,
        "edge_correct":   e_correct,
        "edge_total":     EDGE_COUNT,
        "vertex_acc":     v_correct / VERTEX_COUNT,
        "edge_acc":       e_correct / EDGE_COUNT,
    }


# ── Empty board ────────────────────────────────────────────────────────────

def test_empty_board_no_false_positives(det, empty_setup):
    canonical = draw_canonical_board(empty_setup, add_markers=False)
    state = det.detect(canonical)

    fp_vertices = [vid for vid, p in state.vertices.items() if p is not None]
    fp_edges    = [eid for eid, p in state.edges.items()    if p is not None]
    assert fp_vertices == [], f"False positive vertices on empty board: {fp_vertices}"
    assert fp_edges    == [], f"False positive edges on empty board: {fp_edges}"


def test_empty_board_state_has_all_ids(det, empty_setup):
    """BoardState must include an entry for every vertex and edge."""
    canonical = draw_canonical_board(empty_setup, add_markers=False)
    state = det.detect(canonical)
    assert len(state.vertices) == VERTEX_COUNT
    assert len(state.edges)    == EDGE_COUNT


# ── Single-piece placement ─────────────────────────────────────────────────

@pytest.mark.parametrize("vertex_id", [0, 5, 10, 20, 30, 40, 53])
@pytest.mark.parametrize("player_id", [0, 1, 2, 3])
def test_single_settlement(det, vertex_id, player_id):
    setup = BoardSetup(vertex_owners={vertex_id: player_id})
    canonical = draw_canonical_board(setup, add_markers=False)
    state = det.detect(canonical)
    detected = state.vertices[vertex_id]
    assert detected == player_id, (
        f"Vertex {vertex_id} player {player_id}: expected {player_id}, got {detected}"
    )


@pytest.mark.parametrize("edge_id", [0, 11, 24, 38, 50, 71])
@pytest.mark.parametrize("player_id", [0, 1, 2, 3])
def test_single_road(det, edge_id, player_id):
    setup = BoardSetup(edge_owners={edge_id: player_id})
    canonical = draw_canonical_board(setup, add_markers=False)
    state = det.detect(canonical)
    detected = state.edges[edge_id]
    assert detected == player_id, (
        f"Edge {edge_id} player {player_id}: expected {player_id}, got {detected}"
    )


# ── Multi-piece placement ──────────────────────────────────────────────────

def test_four_players_one_settlement_each(det, simple_setup):
    canonical = draw_canonical_board(simple_setup, add_markers=False)
    state = det.detect(canonical)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.98, f"Vertex accuracy too low: {acc}"
    assert acc["edge_acc"]   >= 0.98, f"Edge accuracy too low: {acc}"


def test_dense_board_accuracy(det, dense_setup):
    canonical = draw_canonical_board(dense_setup, add_markers=False)
    state = det.detect(canonical)
    acc = _accuracy(state, dense_setup)
    assert acc["vertex_acc"] >= 0.95
    assert acc["edge_acc"]   >= 0.95


# ── Lighting variation (canonical, no warp) ────────────────────────────────

@pytest.mark.parametrize("brightness,gamma", [
    (0.45, 1.3),   # dim
    (0.65, 1.1),   # slightly dim
    (1.0,  1.0),   # normal
    (1.5,  0.8),   # bright/washed
])
def test_detection_under_lighting(det, simple_setup, brightness, gamma):
    canonical = draw_canonical_board(simple_setup, add_markers=False)
    lit = apply_lighting(canonical, brightness=brightness, gamma=gamma)
    state = det.detect(lit)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.95, (
        f"brightness={brightness} gamma={gamma}: vertex_acc={acc['vertex_acc']:.2%}"
    )
    assert acc["edge_acc"] >= 0.95, (
        f"brightness={brightness} gamma={gamma}: edge_acc={acc['edge_acc']:.2%}"
    )


# ── Gaussian noise (canonical, no warp) ───────────────────────────────────

@pytest.mark.parametrize("stddev", [5, 10, 18])
def test_detection_with_noise(det, simple_setup, stddev):
    canonical = draw_canonical_board(simple_setup, add_markers=False)
    noisy = add_noise(canonical, stddev=stddev, seed=42)
    state = det.detect(noisy)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.95, f"noise stddev={stddev}: {acc}"
    assert acc["edge_acc"]   >= 0.95, f"noise stddev={stddev}: {acc}"


# ── Shadow ─────────────────────────────────────────────────────────────────

def test_detection_with_shadow(det, simple_setup):
    canonical = draw_canonical_board(simple_setup, add_markers=False)
    shadowed = apply_lighting(canonical, shadow_strength=0.5, shadow_seed=7)
    state = det.detect(shadowed)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.90
    assert acc["edge_acc"]   >= 0.90


# ── BoardState helpers ─────────────────────────────────────────────────────

def test_summary_method(det, simple_setup):
    canonical = draw_canonical_board(simple_setup, add_markers=False)
    state = det.detect(canonical)
    summary = state.summary()
    assert "settlements" in summary
    assert "roads" in summary
