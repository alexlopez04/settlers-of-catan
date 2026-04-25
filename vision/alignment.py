"""
Board-to-canonical homography via colored reference markers.

Physical setup
--------------
Place four bright-colored circular markers (e.g., magenta stickers ~2-3 cm
diameter) at the corners of a rectangular frame that encloses the board.
Their canonical pixel positions are defined by CANONICAL_MARKER_POS.

Default marker color: magenta (~H=146, S=255, V=255 in OpenCV HSV).

The homography H maps camera-image coordinates → canonical coordinates.
warp_to_canonical(image, H) returns the rectified 1000×1000 board image.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import cv2
import numpy as np

# ── Constants ──────────────────────────────────────────────────────────────

CANONICAL_SIZE = 1000

# Canonical (x, y) positions of the 4 reference markers: TL, TR, BR, BL.
# Chosen to leave a comfortable margin around the board.
CANONICAL_MARKER_POS: np.ndarray = np.array(
    [[100.0, 100.0],   # Top-left
     [900.0, 100.0],   # Top-right
     [900.0, 900.0],   # Bottom-right
     [100.0, 900.0]],  # Bottom-left
    dtype=np.float32,
)


# ── Configuration ──────────────────────────────────────────────────────────

@dataclass
class AlignmentConfig:
    """Tunable parameters for marker detection and homography estimation."""
    # HSV range for the reference marker color (default: magenta).
    h_min: int = 135
    h_max: int = 160
    s_min: int = 120
    s_max: int = 255
    v_min: int = 80
    v_max: int = 255
    # Blob area thresholds in camera-image pixels.
    min_area: int = 80
    max_area: int = 8000
    # Canonical marker positions override (None = use CANONICAL_MARKER_POS).
    canonical_positions: Optional[np.ndarray] = None

    @property
    def dst_points(self) -> np.ndarray:
        return self.canonical_positions if self.canonical_positions is not None \
               else CANONICAL_MARKER_POS


# ── Internal helpers ───────────────────────────────────────────────────────

def _order_tl_tr_br_bl(pts: np.ndarray) -> np.ndarray:
    """
    Given an (N, 2) array of 2-D points, select / order 4 corners as
    [TL, TR, BR, BL].  Works for N >= 4 by picking extremal candidates.
    """
    # Sort by Y to split top/bottom halves.
    by_y = pts[np.argsort(pts[:, 1])]
    top2 = by_y[:2]
    bot2 = by_y[-2:]
    tl, tr = top2[np.argsort(top2[:, 0])]
    bl, br = bot2[np.argsort(bot2[:, 0])]
    return np.array([tl, tr, br, bl], dtype=np.float32)


def _detect_marker_centroids(
    image_bgr: np.ndarray,
    cfg: AlignmentConfig,
) -> List[Tuple[float, float]]:
    """Return centroids of blobs matching the marker HSV range."""
    hsv = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2HSV)
    lo = np.array([cfg.h_min, cfg.s_min, cfg.v_min], dtype=np.uint8)
    hi = np.array([cfg.h_max, cfg.s_max, cfg.v_max], dtype=np.uint8)
    mask = cv2.inRange(hsv, lo, hi)

    # Morphological closing to fill small holes in marker blobs.
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    centroids: List[Tuple[float, float]] = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        if cfg.min_area <= area <= cfg.max_area:
            M = cv2.moments(cnt)
            if M["m00"] > 0:
                centroids.append((M["m10"] / M["m00"], M["m01"] / M["m00"]))
    return centroids


# ── Public API ─────────────────────────────────────────────────────────────

class AlignmentError(RuntimeError):
    """Raised when board markers cannot be found or the homography is bad."""


def find_homography(
    image_bgr: np.ndarray,
    cfg: Optional[AlignmentConfig] = None,
) -> np.ndarray:
    """
    Detect the 4 reference markers in *image_bgr* and return the homography
    H that maps camera coordinates → canonical board coordinates.

    Raises AlignmentError if fewer than 4 markers are detected.
    """
    if cfg is None:
        cfg = AlignmentConfig()

    centroids = _detect_marker_centroids(image_bgr, cfg)
    if len(centroids) < 4:
        raise AlignmentError(
            f"Expected 4 reference markers, found {len(centroids)}. "
            "Check that all markers are visible and in range."
        )

    pts = np.array(centroids, dtype=np.float32)

    # If more than 4 candidates (unlikely), pick the 4 that span the largest area
    # by taking the convex hull and picking the 4 extremal points.
    if len(pts) > 4:
        hull_idx = cv2.convexHull(pts, returnPoints=False).flatten()
        hull_pts = pts[hull_idx]
        # Reduce to exactly 4 by ordering and picking corners.
        if len(hull_pts) >= 4:
            pts = _order_tl_tr_br_bl(hull_pts)
        else:
            pts = pts[:4]

    src = _order_tl_tr_br_bl(pts)
    dst = _order_tl_tr_br_bl(cfg.dst_points)

    H, _ = cv2.findHomography(src, dst, cv2.RANSAC, 5.0)
    if H is None:
        raise AlignmentError("findHomography failed – marker layout may be degenerate.")
    return H


def warp_to_canonical(image_bgr: np.ndarray, H: np.ndarray) -> np.ndarray:
    """Apply *H* and produce a CANONICAL_SIZE × CANONICAL_SIZE BGR image."""
    return cv2.warpPerspective(
        image_bgr, H,
        (CANONICAL_SIZE, CANONICAL_SIZE),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=(80, 80, 80),
    )
