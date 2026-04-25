"""
End-to-end integration tests: full pipeline including marker detection,
homography, warp, and piece detection.

These tests simulate the real-world use case:
  1. A canonical board image is generated (with pieces + reference markers).
  2. A random perspective + lighting distortion is applied to simulate camera.
  3. The full BoardDetector pipeline (alignment → warp → detect) runs.
  4. Accuracy is asserted against ground truth.
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import pytest
import numpy as np

from vision.detector import BoardDetector, DetectorConfig
from vision.topology import VERTEX_COUNT, EDGE_COUNT
from vision.tests.board_generator import (
    BoardSetup, SceneConfig, WarpParams, make_test_image,
)


# ── Helper ─────────────────────────────────────────────────────────────────

def _accuracy(state, setup: BoardSetup) -> dict:
    v_correct = sum(
        1 for vid in range(VERTEX_COUNT)
        if state.vertices.get(vid) == setup.vertex_owners.get(vid)
    )
    e_correct = sum(
        1 for eid in range(EDGE_COUNT)
        if state.edges.get(eid) == setup.edge_owners.get(eid)
    )
    return {
        "vertex_acc": v_correct / VERTEX_COUNT,
        "edge_acc":   e_correct / EDGE_COUNT,
    }


# ── Full pipeline — clean (no distortion) ─────────────────────────────────

def test_full_pipeline_clean(simple_setup):
    """End-to-end with no distortion — sanity check."""
    scene = SceneConfig(
        warp=WarpParams(),   # identity warp (rotation=0, perspective=0)
        seed=0,
    )
    img, _ = make_test_image(simple_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.97, f"vertex_acc={acc['vertex_acc']:.2%}"
    assert acc["edge_acc"]   >= 0.97, f"edge_acc={acc['edge_acc']:.2%}"


# ── Camera rotation tolerance ──────────────────────────────────────────────

@pytest.mark.parametrize("angle_deg", [-7, -4, -2, 0, 2, 4, 7])
def test_rotation_tolerance(simple_setup, angle_deg):
    """Detector must work across ±7° camera rotation."""
    scene = SceneConfig(
        warp=WarpParams(rotation_deg=angle_deg),
        seed=1,
    )
    img, _ = make_test_image(simple_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.92, (
        f"angle={angle_deg}°: vertex_acc={acc['vertex_acc']:.2%}"
    )
    assert acc["edge_acc"] >= 0.92, (
        f"angle={angle_deg}°: edge_acc={acc['edge_acc']:.2%}"
    )


# ── Scale / camera height tolerance ───────────────────────────────────────

@pytest.mark.parametrize("scale", [0.92, 0.96, 1.00, 1.04, 1.08])
def test_scale_tolerance(simple_setup, scale):
    """Detector must work when camera is slightly higher/lower (±8% scale)."""
    scene = SceneConfig(
        warp=WarpParams(scale=scale),
        seed=2,
    )
    img, _ = make_test_image(simple_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.92, f"scale={scale}: {acc}"
    assert acc["edge_acc"]   >= 0.92, f"scale={scale}: {acc}"


# ── Lighting conditions ────────────────────────────────────────────────────

@pytest.mark.parametrize("brightness,gamma", [
    (0.50, 1.25),   # dim
    (0.70, 1.10),   # slightly dim
    (1.00, 1.00),   # normal
    (1.40, 0.85),   # bright / washed out
])
def test_lighting_full_pipeline(simple_setup, brightness, gamma):
    """Full pipeline must work across a realistic lighting range."""
    scene = SceneConfig(
        warp=WarpParams.random(max_rot_deg=3.0, seed=5),
        brightness=brightness,
        gamma=gamma,
        seed=5,
    )
    img, _ = make_test_image(simple_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.90, (
        f"brightness={brightness} gamma={gamma}: {acc}"
    )
    assert acc["edge_acc"] >= 0.90, (
        f"brightness={brightness} gamma={gamma}: {acc}"
    )


# ── Combined distortion ─────────────────────────────────────────────────────

@pytest.mark.parametrize("seed", [11, 22, 33, 44])
def test_combined_distortion(simple_setup, seed):
    """
    Random combination of rotation, perspective, dim lighting, shadow, and noise.
    Target: ≥90% accuracy on vertices, ≥88% on edges.
    """
    scene = SceneConfig(
        warp=WarpParams.random(
            max_rot_deg=5.0,
            max_perspective_frac=0.012,
            max_scale_delta=0.04,
            seed=seed,
        ),
        brightness=0.60,
        gamma=1.15,
        shadow_strength=0.30,
        noise_stddev=6.0,
        seed=seed,
    )
    img, _ = make_test_image(simple_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    acc = _accuracy(state, simple_setup)
    assert acc["vertex_acc"] >= 0.90, f"seed={seed}: {acc}"
    assert acc["edge_acc"]   >= 0.88, f"seed={seed}: {acc}"


# ── Dense board under distortion ───────────────────────────────────────────

def test_dense_board_full_pipeline(dense_setup):
    """Denser piece placement should still achieve ≥90% end-to-end."""
    scene = SceneConfig(
        warp=WarpParams.random(max_rot_deg=4.0, seed=50),
        brightness=0.75,
        gamma=1.05,
        noise_stddev=5.0,
        seed=50,
    )
    img, _ = make_test_image(dense_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    acc = _accuracy(state, dense_setup)
    assert acc["vertex_acc"] >= 0.90, f"dense: {acc}"
    assert acc["edge_acc"]   >= 0.88, f"dense: {acc}"


# ── No false positives on empty board ─────────────────────────────────────

def test_empty_board_no_fp_full_pipeline(empty_setup):
    """Under real pipeline conditions, an empty board must report no pieces."""
    scene = SceneConfig(
        warp=WarpParams.random(max_rot_deg=4.0, seed=60),
        brightness=0.85,
        noise_stddev=5.0,
        seed=60,
    )
    img, _ = make_test_image(empty_setup, scene)
    detector = BoardDetector()
    state = detector.detect(img)
    fp_v = [vid for vid, p in state.vertices.items() if p is not None]
    fp_e = [eid for eid, p in state.edges.items()    if p is not None]
    assert len(fp_v) == 0, f"False positive vertices: {fp_v}"
    assert len(fp_e) == 0, f"False positive edges: {fp_e}"
