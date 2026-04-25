"""
Command-line interface for the Catan vision detector.

Usage
-----
    python -m vision.cli image.jpg [--players RED BLUE WHITE ORANGE] [--out out.jpg] [--json]

If --json is given, prints a JSON object:
    {
      "vertices": {"0": null, "1": 0, ...},
      "edges":    {"0": null, "5": 2, ...}
    }

Otherwise prints a human-readable summary table.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2

from .detector import BoardDetector, DetectorConfig
from .alignment import AlignmentConfig, AlignmentError
from .color_classifier import DEFAULT_PLAYER_COLORS, PlayerColor


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python -m vision.cli",
        description="Detect Catan piece placement from a top-down board photo.",
    )
    p.add_argument("image", type=Path, help="Path to input image (JPG/PNG).")
    p.add_argument(
        "--players",
        nargs="+",
        default=None,
        metavar="COLOR",
        help="Player color names for display (e.g. 'red blue white orange').",
    )
    p.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Save annotated canonical image to this path.",
    )
    p.add_argument(
        "--json",
        action="store_true",
        help="Output machine-readable JSON instead of the human summary.",
    )
    p.add_argument(
        "--no-alignment",
        dest="skip_alignment",
        action="store_true",
        help="Skip marker detection (image is already in canonical coordinates).",
    )
    return p


def _player_name(player_id: "Optional[int]", names: list) -> str:
    if player_id is None:
        return "—"
    if player_id < len(names):
        return names[player_id]
    return f"P{player_id}"


def _annotate(canonical: "cv2.Mat", state: "BoardState") -> "cv2.Mat":
    """Draw small colored dots on vertices and edges for visual debugging."""
    import numpy as np
    from .geometry import VERTEX_POS, EDGE_MIDPOINT
    from .tests.board_generator import PLAYER_COLORS_BGR

    img = canonical.copy()
    for vid, player in state.vertices.items():
        if player is None:
            continue
        x, y = VERTEX_POS[vid]
        cv2.circle(img, (int(x), int(y)), 6, PLAYER_COLORS_BGR[player], -1)
        cv2.circle(img, (int(x), int(y)), 6, (0, 0, 0), 1)

    for eid, player in state.edges.items():
        if player is None:
            continue
        x, y = EDGE_MIDPOINT[eid]
        cv2.circle(img, (int(x), int(y)), 4, PLAYER_COLORS_BGR[player], -1)

    return img


def main(argv: "Optional[list]" = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    # Load image.
    image_path: Path = args.image
    if not image_path.exists():
        print(f"error: file not found: {image_path}", file=sys.stderr)
        return 1

    img = cv2.imread(str(image_path))
    if img is None:
        print(f"error: could not decode image: {image_path}", file=sys.stderr)
        return 1

    # Build detector.
    cfg = DetectorConfig(
        skip_alignment=args.skip_alignment,
        player_colors=list(DEFAULT_PLAYER_COLORS),
    )
    detector = BoardDetector(cfg)

    try:
        state = detector.detect(img)
    except AlignmentError as e:
        print(f"error: alignment failed — {e}", file=sys.stderr)
        return 2

    player_names = args.players or [pc.name for pc in DEFAULT_PLAYER_COLORS]

    # Output.
    if args.json:
        data = {
            "vertices": {str(k): v for k, v in state.vertices.items()},
            "edges":    {str(k): v for k, v in state.edges.items()},
        }
        print(json.dumps(data))
    else:
        # Human-readable summary.
        placed_v = {k: v for k, v in state.vertices.items() if v is not None}
        placed_e = {k: v for k, v in state.edges.items()    if v is not None}

        print(f"\nSettlements ({len(placed_v)} placed):")
        if placed_v:
            for vid, pid in sorted(placed_v.items()):
                print(f"  Vertex {vid:3d}  →  {_player_name(pid, player_names)}")
        else:
            print("  (none)")

        print(f"\nRoads ({len(placed_e)} placed):")
        if placed_e:
            for eid, pid in sorted(placed_e.items()):
                print(f"  Edge   {eid:3d}  →  {_player_name(pid, player_names)}")
        else:
            print("  (none)")

    # Optional annotated output image.
    if args.out:
        from .alignment import find_homography, warp_to_canonical
        try:
            if args.skip_alignment:
                canonical = img
            else:
                H = find_homography(img)
                canonical = warp_to_canonical(img, H)
            annotated = _annotate(canonical, state)
            cv2.imwrite(str(args.out), annotated)
            print(f"\nAnnotated image saved to {args.out}")
        except Exception as exc:
            print(f"warning: could not save annotated image: {exc}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
