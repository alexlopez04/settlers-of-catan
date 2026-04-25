"""
vision — Catan piece detection from top-down camera images.

Quick-start
-----------
    import cv2
    from vision import BoardDetector

    image = cv2.imread("board.jpg")
    detector = BoardDetector()
    state = detector.detect(image)  # requires 4 magenta reference markers visible
    print(state.summary())
"""

from .detector import BoardDetector, BoardState, DetectorConfig
from .alignment import AlignmentConfig, AlignmentError
from .color_classifier import PlayerColor, HsvRange, DEFAULT_PLAYER_COLORS
from .geometry import CANONICAL_SIZE, HEX_SIZE, VERTEX_POS, EDGE_MIDPOINT
from .topology import VERTEX_COUNT, EDGE_COUNT, TILE_COUNT

__all__ = [
    "BoardDetector",
    "BoardState",
    "DetectorConfig",
    "AlignmentConfig",
    "AlignmentError",
    "PlayerColor",
    "HsvRange",
    "DEFAULT_PLAYER_COLORS",
    "CANONICAL_SIZE",
    "HEX_SIZE",
    "VERTEX_POS",
    "EDGE_MIDPOINT",
    "VERTEX_COUNT",
    "EDGE_COUNT",
    "TILE_COUNT",
]
