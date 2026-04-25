# Catan Vision — Piece Detector

Top-down camera detection for all 54 vertex (settlement/city) and 72 edge (road)
positions on a standard Settlers of Catan board, with per-player color identification.

---

## Overview

```
Camera image
     │
     ▼
┌─────────────────────────────────────────────────────┐
│ 1. Marker detection                                 │
│    Locate 4 magenta reference stickers → H matrix  │
│                                                     │
│ 2. Perspective warp                                 │
│    Rectify image to 1000×1000 canonical coordinates │
│                                                     │
│ 3. Vertex sampling                                  │
│    Circular patch at each of 54 vertices → color   │
│                                                     │
│ 4. Edge sampling                                    │
│    Rotated strip along each of 72 edges → color    │
│                                                     │
│ 5. HSV classification                               │
│    Match dominant color → player (0–3) or None     │
└─────────────────────────────────────────────────────┘
     │
     ▼
BoardState.vertices[vid] → player_id or None
BoardState.edges[eid]    → player_id or None
```

---

## Physical Setup

1. Attach **four magenta circular stickers** (~2–3 cm diameter) at the corners of
   a rectangular frame that surrounds the board. The exact positions do not matter
   as long as they form a convex quadrilateral that encloses the board.

2. Mount a camera **directly above** the board, roughly centred. The system
   tolerates:
   - Camera rotation up to ±7°
   - Scale variation ±8% (camera height)
   - Small perspective tilt

3. Default player colors are **red, blue, white, orange** (standard Catan sets).
   Custom HSV ranges can be configured — see `DetectorConfig.player_colors`.

---

## Install

```bash
cd vision/
pip install -r requirements.txt
```

---

## Quick Start (Python)

```python
import cv2
from vision import BoardDetector

image = cv2.imread("board_photo.jpg")
detector = BoardDetector()
state = detector.detect(image)   # requires 4 magenta markers visible

# Check vertex 5
print(state.vertices[5])   # → 0 (player 0 = red), or None if empty

# Iterate roads
for edge_id, player in state.edges.items():
    if player is not None:
        print(f"Edge {edge_id}: player {player}")

print(state.summary())
```

---

## CLI

```bash
# Human-readable output
python -m vision.cli board_photo.jpg

# JSON output (machine-readable)
python -m vision.cli board_photo.jpg --json

# Save annotated canonical image
python -m vision.cli board_photo.jpg --out annotated.jpg

# Skip alignment (image already in canonical coords)
python -m vision.cli canonical.jpg --no-alignment
```

---

## Configuration

```python
from vision import BoardDetector, DetectorConfig, AlignmentConfig, PlayerColor, HsvRange

config = DetectorConfig(
    # Sampling window radius around each vertex (pixels, canonical space).
    vertex_sample_radius=14,
    # Half-width of the road-sampling strip perpendicular to the edge.
    edge_sample_half_width=10,
    # Fraction of edge length to sample (centred on midpoint).
    edge_sample_length_frac=0.55,
    # Override player colors for a different piece set.
    player_colors=[
        PlayerColor("red",    [HsvRange(0,10,120,255,60,255), HsvRange(165,180,120,255,60,255)]),
        PlayerColor("blue",   [HsvRange(100,135,100,255,60,255)]),
        PlayerColor("white",  [HsvRange(0,180,0,50,170,255)]),
        PlayerColor("orange", [HsvRange(8,22,140,255,100,255)]),
    ],
    # Alignment config (marker color, size thresholds).
    alignment=AlignmentConfig(
        h_min=135, h_max=160,   # magenta marker hue range (OpenCV 0–180)
        s_min=120, v_min=80,
        min_area=80, max_area=8000,
    ),
)
detector = BoardDetector(config)
```

---

## Board Topology

IDs match the firmware (`board_topology.cpp`) exactly:

| Count | Description                                        |
|-------|----------------------------------------------------|
| 19    | Hex tiles (T00–T18), pointy-top axial coordinates  |
| 54    | Vertices (V00–V53) — settlement / city positions   |
| 72    | Edges (E00–E71) — road positions                   |

See `vision/topology.py` and `docs/board_layout.md` for full ID tables.

---

## Tests

```bash
# Run all 157 tests
python -m pytest vision/tests/ -v

# Coverage report
python -m pytest vision/tests/ --cov=vision --cov-report=term-missing
```

Test suite covers:
- **Topology integrity** — vertex/edge ID tables match firmware
- **Geometry** — canonical vertex positions, edge lengths, canvas bounds
- **Color classifier** — all 4 players, brightness 0.45–1.6×, noisy patches
- **Alignment** — marker detection, homography round-trip, 3 lighting levels
- **Detector (canonical)** — single pieces, multi-player, lighting, noise, shadow
- **Integration (full pipeline)** — rotation ±7°, scale ±8%, combined distortion,
  dim lighting 0.5×, dense board, empty-board false-positive check

---

## Module Structure

```
vision/
├── __init__.py          # Public API re-exports
├── topology.py          # Static board graph data (mirrors firmware)
├── geometry.py          # Canonical pixel positions for vertices/edges
├── color_classifier.py  # HSV-based player piece detection
├── alignment.py         # Marker detection + homography
├── detector.py          # BoardDetector — top-level entry point
├── cli.py               # Command-line tool
├── requirements.txt
└── tests/
    ├── board_generator.py  # Synthetic board image generator
    ├── conftest.py         # Shared pytest fixtures
    ├── test_topology.py
    ├── test_geometry.py
    ├── test_color_classifier.py
    ├── test_alignment.py
    ├── test_detector.py
    └── test_integration.py
```
