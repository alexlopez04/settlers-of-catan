"""
Main board-state detector.

Usage
-----
    from vision import BoardDetector

    detector = BoardDetector()
    state = detector.detect(image_bgr)
    # state.vertices: dict[int, int | None]  — vertex_id → player (0-3) or None
    # state.edges:    dict[int, int | None]  — edge_id   → player (0-3) or None
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

import cv2
import numpy as np

from .topology import VERTEX_COUNT, EDGE_COUNT, EDGE_VERTICES
from .geometry import (
    CANONICAL_SIZE, HEX_SIZE, VERTEX_POS, EDGE_LENGTH,
    _build_vertex_positions, _build_edge_midpoints, _build_edge_lengths,
)
from .color_classifier import PlayerColor, DEFAULT_PLAYER_COLORS, classify_patch
from .alignment import AlignmentConfig, AlignmentError, find_homography, warp_to_canonical


# ── Result types ───────────────────────────────────────────────────────────

@dataclass
class BoardState:
    """Detected occupancy for every vertex and edge on the board."""
    # vertex_id (0-53) → player_id (0-3) or None if empty.
    vertices: Dict[int, Optional[int]] = field(default_factory=dict)
    # edge_id (0-71)   → player_id (0-3) or None if empty.
    edges:    Dict[int, Optional[int]] = field(default_factory=dict)

    def summary(self) -> str:
        placed_v = {k: v for k, v in self.vertices.items() if v is not None}
        placed_e = {k: v for k, v in self.edges.items() if v is not None}
        return (
            f"BoardState: {len(placed_v)} settlements, {len(placed_e)} roads\n"
            f"  Settlements: {placed_v}\n"
            f"  Roads:       {placed_e}"
        )


# ── Detector config ────────────────────────────────────────────────────────

@dataclass
class DetectorConfig:
    # Radius (pixels) of the circular sampling window around each vertex.
    vertex_sample_radius: int = 14
    # Half-width (pixels) of the road-sampling strip, perpendicular to edge.
    edge_sample_half_width: int = 10
    # Fraction of edge length to sample along the edge (centred on midpoint).
    edge_sample_length_frac: float = 0.55
    # Player color definitions.
    player_colors: List[PlayerColor] = field(
        default_factory=lambda: list(DEFAULT_PLAYER_COLORS)
    )
    # Alignment marker configuration.  None → use defaults.
    alignment: Optional[AlignmentConfig] = None
    # Set True when the input image is already in canonical coordinates
    # (e.g. for unit tests).  Skips homography computation.
    skip_alignment: bool = False


# ── Main detector class ────────────────────────────────────────────────────

class BoardDetector:
    """
    Detect Catan piece placement from a top-down BGR camera image.

    The detector:
      1. Finds 4 coloured reference markers → computes homography.
      2. Warps the image to canonical 1000×1000 coordinates.
      3. For each of 54 vertices, samples a circular patch and classifies color.
      4. For each of 72 edges, samples a rotated strip and classifies color.
    """

    def __init__(self, config: Optional[DetectorConfig] = None) -> None:
        self.cfg = config or DetectorConfig()
        self._alignment_cfg = self.cfg.alignment or AlignmentConfig()
        self._vpos: List[Tuple[float, float]] = VERTEX_POS

    # ── Public ──────────────────────────────────────────────────────────────

    def detect(self, image_bgr: np.ndarray) -> BoardState:
        """
        Detect board state from a BGR image.

        Raises AlignmentError if marker detection fails (and skip_alignment
        is False).
        """
        if self.cfg.skip_alignment:
            canonical = image_bgr
        else:
            H = find_homography(image_bgr, self.cfg.alignment)
            canonical = warp_to_canonical(image_bgr, H)

        state = BoardState()
        for vid in range(VERTEX_COUNT):
            state.vertices[vid] = self._detect_vertex(canonical, vid)
        for eid in range(EDGE_COUNT):
            state.edges[eid] = self._detect_edge(canonical, eid)
        return state

    # ── Private sampling ────────────────────────────────────────────────────

    def _detect_vertex(
        self, canonical: np.ndarray, vertex_id: int
    ) -> Optional[int]:
        cx, cy = self._vpos[vertex_id]
        r = self.cfg.vertex_sample_radius
        h_img, w_img = canonical.shape[:2]

        x0 = max(0, int(cx - r))
        y0 = max(0, int(cy - r))
        x1 = min(w_img, int(cx + r) + 1)
        y1 = min(h_img, int(cy + r) + 1)

        if x1 <= x0 or y1 <= y0:
            return None

        # Integer pixel grid over the bounding square.
        xs = np.arange(x0, x1)
        ys = np.arange(y0, y1)
        XX, YY = np.meshgrid(xs, ys)

        # Circular mask centred on the vertex.
        dist_sq = (XX - cx) ** 2 + (YY - cy) ** 2
        circle_mask = (dist_sq <= r * r).astype(np.uint8) * 255

        patch = canonical[YY, XX]  # shape: (H_patch, W_patch, 3)
        return classify_patch(patch, self.cfg.player_colors, circle_mask)

    def _detect_edge(
        self, canonical: np.ndarray, edge_id: int
    ) -> Optional[int]:
        v1, v2 = EDGE_VERTICES[edge_id]
        ax, ay = self._vpos[v1]
        bx, by = self._vpos[v2]

        edge_len = math.hypot(bx - ax, by - ay)
        if edge_len < 1.0:
            return None

        # Unit vectors along and perpendicular to the edge.
        ux = (bx - ax) / edge_len
        uy = (by - ay) / edge_len
        px = -uy   # perpendicular (rotated 90° CCW)
        py =  ux

        half_len = edge_len * self.cfg.edge_sample_length_frac / 2.0
        hw = float(self.cfg.edge_sample_half_width)
        mx, my = (ax + bx) / 2.0, (ay + by) / 2.0

        # Build a 2-D grid of sample points in canonical space.
        n_along = max(1, int(half_len * 2))
        n_perp  = max(1, int(hw * 2))
        ts = np.linspace(-half_len, half_len, n_along)
        ws = np.linspace(-hw, hw, n_perp)

        T, W = np.meshgrid(ts, ws)
        sx = np.clip(mx + T * ux + W * px, 0, canonical.shape[1] - 1)
        sy = np.clip(my + T * uy + W * py, 0, canonical.shape[0] - 1)

        xi = sx.astype(np.int32)
        yi = sy.astype(np.int32)

        patch = canonical[yi, xi]   # shape: (n_perp, n_along, 3)
        return classify_patch(patch, self.cfg.player_colors)
