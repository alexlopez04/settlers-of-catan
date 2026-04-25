"""
HSV-based player piece color classifier.

OpenCV HSV convention: H ∈ [0, 180], S ∈ [0, 255], V ∈ [0, 255].

Hue is the primary discriminant (robust to brightness changes).
Saturation separates vivid player colors from empty/white patches.
The V threshold is kept low to tolerate shadowed regions.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import cv2
import numpy as np


# ── Data classes ───────────────────────────────────────────────────────────

@dataclass(frozen=True)
class HsvRange:
    """A single inclusive HSV band."""
    h_min: int
    h_max: int
    s_min: int
    s_max: int
    v_min: int
    v_max: int

    def lower_upper(self) -> Tuple[np.ndarray, np.ndarray]:
        lo = np.array([self.h_min, self.s_min, self.v_min], dtype=np.uint8)
        hi = np.array([self.h_max, self.s_max, self.v_max], dtype=np.uint8)
        return lo, hi


@dataclass
class PlayerColor:
    """Color definition for one player. Multiple HSV ranges support hue wrap-around (e.g. red)."""
    name: str
    ranges: List[HsvRange]
    # Minimum fraction of (masked) pixels that must match this color to count.
    min_fraction: float = 0.15


# ── Default four-player color palette ─────────────────────────────────────
#
# Designed for common Catan piece sets. Tune per physical set if needed.
# Four distinct colors: red, blue, white, orange.

DEFAULT_PLAYER_COLORS: List[PlayerColor] = [
    PlayerColor(
        name="red",
        ranges=[
            HsvRange(h_min=0,   h_max=10,  s_min=120, s_max=255, v_min=60, v_max=255),
            HsvRange(h_min=165, h_max=180, s_min=120, s_max=255, v_min=60, v_max=255),
        ],
        min_fraction=0.15,
    ),
    PlayerColor(
        name="blue",
        ranges=[
            HsvRange(h_min=100, h_max=135, s_min=100, s_max=255, v_min=60, v_max=255),
        ],
        min_fraction=0.15,
    ),
    PlayerColor(
        name="white",
        ranges=[
            HsvRange(h_min=0, h_max=180, s_min=0, s_max=50, v_min=170, v_max=255),
        ],
        min_fraction=0.15,
    ),
    PlayerColor(
        name="orange",
        ranges=[
            HsvRange(h_min=8, h_max=22, s_min=140, s_max=255, v_min=100, v_max=255),
        ],
        min_fraction=0.15,
    ),
]


# ── Classifier ─────────────────────────────────────────────────────────────

def classify_patch(
    patch_bgr: np.ndarray,
    player_colors: List[PlayerColor],
    mask: Optional[np.ndarray] = None,
) -> Optional[int]:
    """
    Classify the dominant player color in *patch_bgr*.

    Parameters
    ----------
    patch_bgr:
        BGR image of shape (H, W, 3). Can be non-contiguous.
    player_colors:
        Ordered list of player color definitions. Return value is the list
        index of the winning player.
    mask:
        Optional binary mask of shape (H, W) with values 0 or 255.
        Only pixels where mask > 0 are considered.

    Returns
    -------
    Player index (0-based) or None if no color exceeds its min_fraction.
    """
    if patch_bgr is None or patch_bgr.size == 0:
        return None

    # Ensure contiguous C-order array that OpenCV can handle.
    patch_bgr = np.ascontiguousarray(patch_bgr)
    if patch_bgr.ndim != 3 or patch_bgr.shape[2] != 3:
        return None

    hsv = cv2.cvtColor(patch_bgr, cv2.COLOR_BGR2HSV)

    # Total number of active pixels.
    if mask is not None:
        total = int(np.count_nonzero(mask))
    else:
        total = hsv.shape[0] * hsv.shape[1]

    if total == 0:
        return None

    best_player: Optional[int] = None
    best_fraction: float = 0.0

    for player_id, pc in enumerate(player_colors):
        color_mask = np.zeros(hsv.shape[:2], dtype=np.uint8)
        for rng in pc.ranges:
            lo, hi = rng.lower_upper()
            color_mask |= cv2.inRange(hsv, lo, hi)

        if mask is not None:
            color_mask &= mask

        fraction = float(np.count_nonzero(color_mask)) / total
        if fraction > best_fraction and fraction >= pc.min_fraction:
            best_fraction = fraction
            best_player = player_id

    return best_player
