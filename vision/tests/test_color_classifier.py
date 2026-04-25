"""
Tests for the HSV color classifier.
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import numpy as np
import cv2
import pytest

from vision.color_classifier import (
    classify_patch, HsvRange, PlayerColor, DEFAULT_PLAYER_COLORS,
)
from vision.tests.board_generator import PLAYER_COLORS_BGR


# ── Helpers ────────────────────────────────────────────────────────────────

def solid_patch(bgr: tuple, size: int = 30) -> np.ndarray:
    """Return a solid-color BGR patch."""
    return np.full((size, size, 3), bgr, dtype=np.uint8)


def circular_mask(size: int = 30) -> np.ndarray:
    """Return a circular mask that covers most of the patch."""
    mask = np.zeros((size, size), dtype=np.uint8)
    cv2.circle(mask, (size // 2, size // 2), size // 2 - 1, 255, -1)
    return mask


# ── Player color detection ─────────────────────────────────────────────────

@pytest.mark.parametrize("player_id", [0, 1, 2, 3])
def test_detect_pure_player_color(player_id):
    """Solid patches of each player color must be correctly classified."""
    bgr = PLAYER_COLORS_BGR[player_id]
    patch = solid_patch(bgr)
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
    assert result == player_id, (
        f"Expected player {player_id} (BGR={bgr}), got {result}"
    )


@pytest.mark.parametrize("player_id", [0, 1, 2, 3])
def test_detect_with_circular_mask(player_id):
    """Detection should work when only the circular region is masked in."""
    bgr = PLAYER_COLORS_BGR[player_id]
    patch = solid_patch(bgr, size=30)
    mask = circular_mask(30)
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS, mask)
    assert result == player_id


def test_empty_patch_returns_none():
    patch = solid_patch((75, 75, 75))   # board background gray
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
    assert result is None


def test_tile_color_returns_none():
    """Tile surface colors should not be mistaken for player pieces."""
    # Forest green, pasture green, sandy desert — none should trigger a player match.
    for bgr in [(30, 100, 25), (55, 175, 65), (45, 195, 185), (85, 85, 85)]:
        patch = solid_patch(bgr)
        result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
        assert result is None, f"Tile color {bgr} incorrectly matched player {result}"


def test_zero_size_patch_returns_none():
    patch = np.zeros((0, 0, 3), dtype=np.uint8)
    assert classify_patch(patch, DEFAULT_PLAYER_COLORS) is None


def test_empty_mask_returns_none():
    """If the mask is all-zero, nothing can match."""
    patch = solid_patch(PLAYER_COLORS_BGR[0])
    mask = np.zeros((30, 30), dtype=np.uint8)   # all masked OUT
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS, mask)
    assert result is None


@pytest.mark.parametrize("player_id", [0, 1, 2, 3])
def test_piece_with_noisy_background(player_id):
    """
    Patch: center 14×14 pixels = player color, border = gray background.
    The piece must still be detected even when it occupies ~22% of the patch.
    """
    patch = solid_patch((75, 75, 75), size=30)   # background
    bgr = PLAYER_COLORS_BGR[player_id]
    patch[8:22, 8:22] = bgr                       # piece in center
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
    assert result == player_id, f"Player {player_id} not detected in noisy patch"


# ── Lighting variation ─────────────────────────────────────────────────────

@pytest.mark.parametrize("brightness", [0.45, 0.6, 0.8, 1.0, 1.4])
@pytest.mark.parametrize("player_id", [0, 1, 3])   # skip white (degrades under extreme dark)
def test_detect_under_brightness_variation(player_id, brightness):
    """Detection must succeed across a wide brightness range."""
    bgr = PLAYER_COLORS_BGR[player_id]
    patch = np.clip(
        np.full((30, 30, 3), bgr, dtype=np.float32) * brightness,
        0, 255
    ).astype(np.uint8)
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
    assert result == player_id, (
        f"Player {player_id}, brightness={brightness:.2f}: expected {player_id}, got {result}"
    )


def test_white_piece_under_moderate_brightness():
    """White (player 2) should be detectable even if slightly dimmed."""
    bgr = PLAYER_COLORS_BGR[2]
    for brightness in (0.75, 1.0, 1.2):
        patch = np.clip(
            np.full((30, 30, 3), bgr, dtype=np.float32) * brightness,
            0, 255
        ).astype(np.uint8)
        result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
        assert result == 2, f"White not detected at brightness={brightness}"


# ── Custom color config ────────────────────────────────────────────────────

def test_custom_player_colors():
    """A custom single-range config should work end-to-end."""
    green_config = [
        PlayerColor(
            name="green",
            ranges=[HsvRange(h_min=55, h_max=85, s_min=100, s_max=255, v_min=60, v_max=255)],
            min_fraction=0.15,
        )
    ]
    # Pure green in BGR is (0, 200, 0).
    patch = solid_patch((0, 200, 0))
    result = classify_patch(patch, green_config)
    assert result == 0


def test_min_fraction_threshold():
    """If the piece occupies less than min_fraction of the patch, return None."""
    # Make a tiny 3×3 piece in a 30×30 patch: ≈1% coverage.
    patch = solid_patch((75, 75, 75), size=30)
    patch[14:17, 14:17] = PLAYER_COLORS_BGR[0]    # only 9/900 ≈ 1%
    result = classify_patch(patch, DEFAULT_PLAYER_COLORS)
    assert result is None, "Tiny piece should not exceed min_fraction"
