"""
Tests for board alignment (marker detection + homography).
"""
from __future__ import annotations
from typing import Optional

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import numpy as np
import cv2
import pytest

from vision.alignment import (
    AlignmentConfig, AlignmentError,
    find_homography, warp_to_canonical,
    CANONICAL_MARKER_POS, CANONICAL_SIZE, _order_tl_tr_br_bl,
)
from vision.tests.board_generator import (
    BoardSetup, SceneConfig, WarpParams,
    draw_canonical_board, apply_warp, make_test_image,
    MARKER_COLOR_BGR,
)


# ── Helpers ────────────────────────────────────────────────────────────────

def _make_marker_image(warp_params: "Optional[WarpParams]" = None) -> np.ndarray:
    """Return a board image that contains markers, optionally warped."""
    setup = BoardSetup(seed=0)
    scene = SceneConfig(warp=warp_params, seed=0)
    img, _ = make_test_image(setup, scene)
    return img


# ── _order_tl_tr_br_bl ────────────────────────────────────────────────────

def test_order_canonical_markers():
    """Ordering the default canonical positions must return them TL, TR, BR, BL."""
    ordered = _order_tl_tr_br_bl(CANONICAL_MARKER_POS)
    # TL has smallest x and smallest y
    assert ordered[0, 0] < ordered[1, 0]   # TL.x < TR.x
    assert ordered[0, 1] < ordered[3, 1]   # TL.y < BL.y
    assert ordered[1, 1] < ordered[2, 1]   # TR.y < BR.y


def test_order_arbitrary_points():
    pts = np.array([[9, 1], [1, 1], [1, 9], [9, 9]], dtype=np.float32)
    ordered = _order_tl_tr_br_bl(pts)
    assert list(ordered[0]) == [1, 1]   # TL
    assert list(ordered[1]) == [9, 1]   # TR
    assert list(ordered[2]) == [9, 9]   # BR
    assert list(ordered[3]) == [1, 9]   # BL


# ── find_homography on canonical image (no warp, markers are at known spots) ──

def test_find_homography_identity():
    """
    On an un-warped canonical image the recovered H should map
    the detected markers back to near their canonical positions.
    """
    setup = BoardSetup(seed=1)
    canonical = draw_canonical_board(setup, add_markers=True)
    H = find_homography(canonical)

    for mx, my in CANONICAL_MARKER_POS:
        pt_src = np.array([[mx, my]], dtype=np.float32)
        pt_dst = cv2.perspectiveTransform(pt_src[np.newaxis], H)[0, 0]
        assert abs(pt_dst[0] - mx) < 5.0, f"Marker x off: {pt_dst[0]:.1f} vs {mx}"
        assert abs(pt_dst[1] - my) < 5.0, f"Marker y off: {pt_dst[1]:.1f} vs {my}"


def test_find_homography_raises_without_markers():
    """An all-gray image has no markers → should raise AlignmentError."""
    blank = np.full((1000, 1000, 3), (75, 75, 75), dtype=np.uint8)
    with pytest.raises(AlignmentError):
        find_homography(blank)


def test_find_homography_raises_with_three_markers():
    """Only 3 visible markers must raise AlignmentError."""
    setup = BoardSetup(seed=2)
    canonical = draw_canonical_board(setup, add_markers=True)
    # Overwrite one marker region with background color.
    mx, my = map(int, CANONICAL_MARKER_POS[0])
    cv2.circle(canonical, (mx, my), 20, (75, 75, 75), -1)
    with pytest.raises(AlignmentError):
        find_homography(canonical)


# ── find_homography + warp round-trip under perspective distortion ──────────

@pytest.mark.parametrize("seed", [10, 20, 30])
def test_homography_round_trip(seed):
    """
    Warp a canonical board image, then detect markers and re-warp.
    The recovered canonical image should have markers near their
    expected canonical positions.
    """
    warp_params = WarpParams.random(
        max_rot_deg=5.0, max_perspective_frac=0.010, seed=seed
    )
    scene = SceneConfig(warp=warp_params, seed=seed)
    setup = BoardSetup(seed=seed)
    warped_img, _ = make_test_image(setup, scene)

    H = find_homography(warped_img)
    recovered = warp_to_canonical(warped_img, H)

    # In the recovered image, each marker position should be visible and magenta.
    cfg = AlignmentConfig()
    # Re-detect markers in the recovered image — they should be close to canonical.
    recovered_H = find_homography(recovered)
    for mx, my in CANONICAL_MARKER_POS:
        pt_src = np.array([[mx, my]], dtype=np.float32)
        pt_dst = cv2.perspectiveTransform(pt_src[np.newaxis], recovered_H)[0, 0]
        err = np.hypot(pt_dst[0] - mx, pt_dst[1] - my)
        assert err < 20.0, (
            f"Seed {seed}: marker ({mx},{my}) recovered to ({pt_dst[0]:.1f},{pt_dst[1]:.1f}), "
            f"error={err:.1f}px"
        )


# ── warp_to_canonical output properties ───────────────────────────────────

def test_warp_output_shape():
    setup = BoardSetup(seed=5)
    canonical = draw_canonical_board(setup, add_markers=True)
    H = find_homography(canonical)
    out = warp_to_canonical(canonical, H)
    assert out.shape == (CANONICAL_SIZE, CANONICAL_SIZE, 3)
    assert out.dtype == np.uint8


def test_warp_preserves_board_content():
    """
    After the round-trip warp, the output should still look like a board
    (lots of colored pixels, not all gray).
    """
    setup = BoardSetup(seed=6)
    canonical = draw_canonical_board(setup, add_markers=True)
    H = find_homography(canonical)
    out = warp_to_canonical(canonical, H)
    # Check the center region is not uniform gray.
    center = out[400:600, 400:600]
    assert center.std() > 5.0, "Board centre should have color variation"


# ── Marker detection robustness under lighting ─────────────────────────────

@pytest.mark.parametrize("brightness", [0.5, 0.7, 1.0, 1.4])
def test_markers_detectable_under_brightness(brightness):
    """Markers must be found at various brightness levels."""
    setup = BoardSetup(seed=3)
    canonical = draw_canonical_board(setup, add_markers=True)
    from vision.tests.board_generator import apply_lighting
    lit = apply_lighting(canonical, brightness=brightness)
    H = find_homography(lit)   # should not raise
    assert H is not None
    assert H.shape == (3, 3)
