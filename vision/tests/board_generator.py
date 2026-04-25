"""
Synthetic top-down Catan board image generator for testing.

Generates fully procedural images that can be fed into BoardDetector to
validate detection accuracy across a range of conditions.

Pipeline
--------
1. draw_canonical_board()  — draw the board in clean canonical space
2. apply_warp()            — random perspective / rotation distortion
3. apply_lighting()        — brightness / gamma / shadow variations
4. add_noise()             — Gaussian pixel noise
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

import cv2
import numpy as np

# Allow running tests from any working directory by inserting the project root.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from vision.topology import TILE_HEX_COORDS, EDGE_VERTICES
from vision.geometry import (
    CANONICAL_SIZE, HEX_SIZE, VERTEX_POS,
    tile_center, corner_pos,
)
from vision.alignment import CANONICAL_MARKER_POS


# ── Visual constants ───────────────────────────────────────────────────────

# Tile biome colors in BGR.
BIOME_COLORS_BGR: Dict[str, Tuple[int, int, int]] = {
    "forest":   ( 30, 100,  25),  # dark green       H≈62°  S≈191  V≈100
    "pasture":  ( 55, 175,  65),  # medium green      H≈57°  S≈158  V≈175
    "field":    ( 20, 185, 195),  # golden wheat      H≈28°  S≈229  V≈195
    "hill":     ( 65,  85, 110),  # muted clay/tan    H≈13°  S≈104  V≈110  (S<120 avoids all player thresholds)
    "mountain": ( 85,  85,  85),  # slate gray        S=0
    "desert":   ( 45, 195, 185),  # sandy tan         H≈32°  S≈200  V≈195
    "water":    (130, 100,  60),  # blue-gray
}

# Standard distribution: 4F 4P 4Fi 3H 3M 1D — same as firmware randomizeBoardLayout.
_BIOME_SEQUENCE: List[str] = (
    ["forest"] * 4 + ["pasture"] * 4 + ["field"] * 4
    + ["hill"] * 3 + ["mountain"] * 3 + ["desert"] * 1
)

# Player piece colors in BGR.  Must align with DEFAULT_PLAYER_COLORS in color_classifier.
PLAYER_COLORS_BGR: List[Tuple[int, int, int]] = [
    (  0,   0, 215),  # 0: red
    (215,   0,   0),  # 1: blue
    (240, 240, 240),  # 2: white
    (  0, 130, 255),  # 3: orange
]

# Reference marker color: magenta (matches AlignmentConfig defaults: H≈146).
MARKER_COLOR_BGR: Tuple[int, int, int] = (255, 0, 220)
MARKER_RADIUS_PX: int = 18

# Piece dimensions in canonical pixels.
SETTLEMENT_RADIUS_PX: int = 12
ROAD_THICKNESS_PX:    int = 12
ROAD_LENGTH_FRAC:     float = 0.70   # fraction of edge length to draw


# ── Board state / ground truth ─────────────────────────────────────────────

@dataclass
class BoardSetup:
    """Ground-truth piece placement used to generate a test image."""
    # biome per tile (len 19); None → use a random sequence.
    biomes: Optional[List[str]] = None
    # vertex_id → player_id (0-3)
    vertex_owners: Dict[int, int] = field(default_factory=dict)
    # edge_id → player_id (0-3)
    edge_owners: Dict[int, int] = field(default_factory=dict)
    # Random seed for board generation; None → random each call.
    seed: Optional[int] = None

    def resolved_biomes(self) -> List[str]:
        if self.biomes is not None:
            return list(self.biomes)
        rng = random.Random(self.seed)
        seq = list(_BIOME_SEQUENCE)
        rng.shuffle(seq)
        return seq


# ── Drawing routines ───────────────────────────────────────────────────────

def draw_canonical_board(setup: BoardSetup, add_markers: bool = True) -> np.ndarray:
    """
    Render the board in clean 1000×1000 canonical coordinates.

    Returns a BGR uint8 image.
    """
    img = np.full((CANONICAL_SIZE, CANONICAL_SIZE, 3), (75, 75, 75), dtype=np.uint8)

    biomes = setup.resolved_biomes()

    # ── Draw hex tiles ──────────────────────────────────────────────────────
    for tile_id, (q, r) in enumerate(TILE_HEX_COORDS):
        cx, cy = tile_center(q, r)
        corners_px = [
            (int(round(corner_pos(cx, cy, c)[0])),
             int(round(corner_pos(cx, cy, c)[1])))
            for c in range(6)
        ]
        pts = np.array(corners_px, dtype=np.int32)
        color = BIOME_COLORS_BGR.get(biomes[tile_id], (100, 100, 100))
        cv2.fillPoly(img, [pts], color)
        cv2.polylines(img, [pts], isClosed=True, color=(35, 35, 35), thickness=1)

    # ── Draw roads (edges) ─────────────────────────────────────────────────
    for edge_id, player_id in setup.edge_owners.items():
        v1, v2 = EDGE_VERTICES[edge_id]
        x1, y1 = VERTEX_POS[v1]
        x2, y2 = VERTEX_POS[v2]
        mx, my = (x1 + x2) / 2.0, (y1 + y2) / 2.0
        dx, dy = x2 - x1, y2 - y1
        f = ROAD_LENGTH_FRAC / 2.0
        p1 = (int(round(mx - dx * f)), int(round(my - dy * f)))
        p2 = (int(round(mx + dx * f)), int(round(my + dy * f)))
        color = PLAYER_COLORS_BGR[player_id]
        cv2.line(img, p1, p2, color, ROAD_THICKNESS_PX)

    # ── Draw settlements (vertices) ────────────────────────────────────────
    for vertex_id, player_id in setup.vertex_owners.items():
        vx, vy = VERTEX_POS[vertex_id]
        color = PLAYER_COLORS_BGR[player_id]
        cv2.circle(img, (int(round(vx)), int(round(vy))), SETTLEMENT_RADIUS_PX, color, -1)
        cv2.circle(img, (int(round(vx)), int(round(vy))), SETTLEMENT_RADIUS_PX, (15, 15, 15), 1)

    # ── Draw reference markers ─────────────────────────────────────────────
    if add_markers:
        for mx, my in CANONICAL_MARKER_POS:
            cv2.circle(img, (int(mx), int(my)), MARKER_RADIUS_PX, MARKER_COLOR_BGR, -1)
            cv2.circle(img, (int(mx), int(my)), MARKER_RADIUS_PX, (20, 20, 20), 1)

    return img


# ── Distortion helpers ─────────────────────────────────────────────────────

@dataclass
class WarpParams:
    """Parameters controlling the perspective/rotation warp applied to the canonical image."""
    # Rotation of camera around vertical axis (degrees).
    rotation_deg:    float = 0.0
    # Maximum corner offset as a fraction of image size (simulates tilt/perspective).
    perspective_frac: float = 0.0
    # Uniform scale factor (simulates camera height change).
    scale:           float = 1.0
    # Translation in canonical pixels.
    tx:              float = 0.0
    ty:              float = 0.0

    @staticmethod
    def random(
        max_rot_deg: float = 5.0,
        max_perspective_frac: float = 0.015,
        max_scale_delta: float = 0.05,
        max_translate_px: float = 20.0,
        seed: Optional[int] = None,
    ) -> "WarpParams":
        rng = random.Random(seed)
        return WarpParams(
            rotation_deg=rng.uniform(-max_rot_deg, max_rot_deg),
            perspective_frac=rng.uniform(0, max_perspective_frac),
            scale=1.0 + rng.uniform(-max_scale_delta, max_scale_delta),
            tx=rng.uniform(-max_translate_px, max_translate_px),
            ty=rng.uniform(-max_translate_px, max_translate_px),
        )


def apply_warp(
    img: np.ndarray,
    params: Optional[WarpParams] = None,
    seed: Optional[int] = None,
) -> Tuple[np.ndarray, np.ndarray]:
    """
    Apply a perspective warp to *img*.

    Returns (warped_image, H) where H is the 3×3 homography that maps
    canonical → warped (i.e. the forward transform applied to the image).
    """
    if params is None:
        params = WarpParams.random(seed=seed)

    h, w = img.shape[:2]
    cx, cy = w / 2.0, h / 2.0

    # Start from bounding rectangle corners.
    src = np.float32([[0, 0], [w, 0], [w, h], [0, h]])

    # Rotation matrix (around image centre).
    angle_rad = math.radians(params.rotation_deg)
    cos_a, sin_a = math.cos(angle_rad), math.sin(angle_rad)

    def rot(pt: np.ndarray) -> np.ndarray:
        dx, dy = pt[0] - cx, pt[1] - cy
        return np.array([cx + dx * cos_a - dy * sin_a,
                         cy + dx * sin_a + dy * cos_a], dtype=np.float64)

    # Build destination corners: rotate, scale from centre, translate, add random perspective.
    rng = random.Random(seed)
    persp = params.perspective_frac * w

    dst = np.zeros_like(src)
    for i, pt in enumerate(src):
        p = rot(pt)
        # Scale around centre.
        p = np.array([
            cx + (p[0] - cx) * params.scale + params.tx,
            cy + (p[1] - cy) * params.scale + params.ty,
        ])
        # Add per-corner random perspective nudge.
        p[0] += rng.uniform(-persp, persp)
        p[1] += rng.uniform(-persp, persp)
        dst[i] = p

    H = cv2.getPerspectiveTransform(src, dst)
    warped = cv2.warpPerspective(
        img, H, (w, h),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=(75, 75, 75),
    )
    return warped, H


# ── Lighting helpers ───────────────────────────────────────────────────────

def apply_lighting(
    img: np.ndarray,
    brightness: float = 1.0,
    gamma: float = 1.0,
    shadow_strength: float = 0.0,
    shadow_seed: Optional[int] = None,
) -> np.ndarray:
    """
    Simulate lighting variation.

    Parameters
    ----------
    brightness:
        Multiplicative brightness factor (e.g. 0.5 = half as bright).
    gamma:
        Gamma correction exponent (>1 = darker, <1 = brighter/washed out).
    shadow_strength:
        0 = no shadow; 1 = full shadow (one quadrant near-black).
    shadow_seed:
        RNG seed for shadow shape.
    """
    result = img.astype(np.float32)

    # Brightness.
    result *= brightness

    # Gamma.
    if gamma != 1.0:
        result = np.power(np.clip(result / 255.0, 0, 1), gamma) * 255.0

    # Soft shadow: a smoothly varying dark gradient over one quadrant.
    if shadow_strength > 0:
        rng = random.Random(shadow_seed)
        h, w = img.shape[:2]
        # Random linear gradient direction.
        angle = rng.uniform(0, math.tau)
        xx, yy = np.meshgrid(np.arange(w), np.arange(h))
        proj = xx * math.cos(angle) + yy * math.sin(angle)
        proj_norm = (proj - proj.min()) / (proj.max() - proj.min() + 1e-6)
        # Only shadow the "dark" half.
        shadow_map = 1.0 - shadow_strength * np.maximum(0.0, proj_norm - 0.5) * 2.0
        result *= shadow_map[:, :, np.newaxis]

    return np.clip(result, 0, 255).astype(np.uint8)


def add_noise(img: np.ndarray, stddev: float = 0.0, seed: Optional[int] = None) -> np.ndarray:
    """Add Gaussian pixel noise."""
    if stddev <= 0:
        return img.copy()
    rng = np.random.default_rng(seed)
    noise = rng.normal(0, stddev, img.shape)
    return np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)


# ── High-level test-image factory ──────────────────────────────────────────

@dataclass
class SceneConfig:
    """Full configuration for one synthetic test image."""
    warp:             Optional[WarpParams] = None   # None → identity (no warp)
    brightness:       float = 1.0
    gamma:            float = 1.0
    shadow_strength:  float = 0.0
    noise_stddev:     float = 0.0
    seed:             Optional[int] = None


def make_test_image(
    setup: BoardSetup,
    scene: Optional[SceneConfig] = None,
    return_canonical: bool = False,
) -> Tuple[np.ndarray, Optional[np.ndarray]]:
    """
    Generate a synthetic test image with the given board setup and scene conditions.

    Parameters
    ----------
    setup:
        Piece placement + biomes.
    scene:
        Distortion / lighting parameters.  None → identity (clean canonical).
    return_canonical:
        If True, also return the clean canonical image.

    Returns
    -------
    (final_image, canonical_image | None)
    """
    if scene is None:
        scene = SceneConfig()

    canonical = draw_canonical_board(setup, add_markers=(scene.warp is not None))

    if scene.warp is not None:
        img, _ = apply_warp(canonical, scene.warp, seed=scene.seed)
    else:
        img = canonical.copy()

    img = apply_lighting(
        img,
        brightness=scene.brightness,
        gamma=scene.gamma,
        shadow_strength=scene.shadow_strength,
        shadow_seed=scene.seed,
    )
    img = add_noise(img, stddev=scene.noise_stddev, seed=scene.seed)

    return img, (canonical if return_canonical else None)
