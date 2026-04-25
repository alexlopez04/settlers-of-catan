"""
Shared fixtures and helpers for the vision test suite.
"""

import sys
import os

# Make the repo root importable regardless of working directory.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

import pytest
import numpy as np
import cv2

from vision.topology import VERTEX_COUNT, EDGE_COUNT
from vision.tests.board_generator import BoardSetup, SceneConfig, WarpParams, make_test_image


# ── Standard board setups ──────────────────────────────────────────────────

@pytest.fixture
def empty_setup() -> BoardSetup:
    return BoardSetup(seed=42)


@pytest.fixture
def simple_setup() -> BoardSetup:
    """Four settlements and four roads — one per player."""
    return BoardSetup(
        seed=42,
        vertex_owners={0: 0, 10: 1, 20: 2, 30: 3},
        edge_owners={0: 0, 11: 1, 24: 2, 38: 3},
    )


@pytest.fixture
def dense_setup() -> BoardSetup:
    """A more heavily populated board for stress-testing."""
    return BoardSetup(
        seed=7,
        vertex_owners={
            0: 0,  2: 0,  4: 0,
            1: 1,  3: 1,  5: 1,
            6: 2,  8: 2, 10: 2,
            7: 3,  9: 3, 11: 3,
        },
        edge_owners={
            0: 0,  1: 0,  2: 0,
            6: 1,  7: 1,  8: 1,
           11: 2, 12: 2, 13: 2,
           16: 3, 17: 3, 18: 3,
        },
    )


# ── Scene presets ──────────────────────────────────────────────────────────

@pytest.fixture
def clean_scene() -> SceneConfig:
    return SceneConfig()   # identity — no warp, no distortion


@pytest.fixture
def dim_scene() -> SceneConfig:
    return SceneConfig(brightness=0.45, gamma=1.3, seed=1)


@pytest.fixture
def bright_scene() -> SceneConfig:
    return SceneConfig(brightness=1.6, gamma=0.75, seed=2)


@pytest.fixture
def shadow_scene() -> SceneConfig:
    return SceneConfig(shadow_strength=0.55, seed=3)


@pytest.fixture
def noisy_scene() -> SceneConfig:
    return SceneConfig(noise_stddev=12.0, seed=4)


@pytest.fixture
def warped_scene() -> SceneConfig:
    return SceneConfig(
        warp=WarpParams.random(
            max_rot_deg=6.0,
            max_perspective_frac=0.012,
            max_scale_delta=0.04,
            max_translate_px=18.0,
            seed=10,
        ),
        seed=10,
    )


@pytest.fixture
def harsh_scene() -> SceneConfig:
    """Combined warp + dim + noise — worst-case test."""
    return SceneConfig(
        warp=WarpParams.random(
            max_rot_deg=7.0,
            max_perspective_frac=0.015,
            seed=99,
        ),
        brightness=0.5,
        gamma=1.2,
        noise_stddev=8.0,
        shadow_strength=0.35,
        seed=99,
    )
