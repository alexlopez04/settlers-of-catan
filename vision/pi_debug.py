#!/usr/bin/env python3
"""
pi_debug.py — Live debug UI for the Catan CV pipeline on Raspberry Pi.

Displays a composite OpenCV window with three regions:

  ┌──────────────────────────┬──────────────────────────┐
  │  Raw camera (640×640)    │  Canonical board (640×640) │
  │  · Magenta marker         │  · Empty vertices (gray)  │
  │    crosshairs             │  · Occupied vertices       │
  │  · Alignment quad         │    (player color)          │
  │  · HSV mask (toggle 'm') │  · Occupied edge midpoints │
  └──────────────────────────┴──────────────────────────┤
  │  FPS · det ms · ALIGN · SERIAL TX · per-player count │
  └────────────────────────────────────────────────────┘

Keyboard shortcuts:
  q / Esc  — quit
  m        — toggle HSV marker-mask overlay on raw panel
  p        — pause / resume detection

Usage:
  python -m vision.pi_debug
  python -m vision.pi_debug --port /dev/ttyAMA0 --baud 115200
  python -m vision.pi_debug --no-serial          # display-only, no UART TX
  python -m vision.pi_debug --fullscreen         # for Pi HDMI output
  python -m vision.pi_debug --simulate IMAGE     # static image (no camera)
"""
from __future__ import annotations

import argparse
import logging
import sys
import time
from typing import Dict, List, Optional, Tuple

import cv2
import numpy as np

LOG = logging.getLogger("pi_debug")

# ── Display geometry ───────────────────────────────────────────────────────

PANEL_SIZE  = 640   # width and height of each panel (raw + canonical)
STATUS_H    = 70    # status bar height
DISPLAY_W   = PANEL_SIZE * 2     # 1280
DISPLAY_H   = PANEL_SIZE + STATUS_H  # 710

# ── Player colors / names ──────────────────────────────────────────────────

# BGR to match DEFAULT_PLAYER_COLORS order: red, blue, white, orange
_PLAYER_BGR: List[Tuple[int, int, int]] = [
    (0,   30,  210),   # red
    (200, 80,  20 ),   # blue
    (210, 210, 210),   # white
    (0,   140, 255),   # orange
]
_PLAYER_NAMES = ["Red", "Blue", "White", "Orange"]

# ── Drawing helpers ────────────────────────────────────────────────────────

_FONT       = cv2.FONT_HERSHEY_SIMPLEX
_THICK_OUTLINE = 2

def _text(
    img: np.ndarray,
    label: str,
    pos: Tuple[int, int],
    scale: float = 0.55,
    color: Tuple[int, int, int] = (220, 220, 220),
    thickness: int = 1,
) -> None:
    """Draw text with a thin black outline for legibility on any background."""
    cv2.putText(img, label, pos, _FONT, scale, (0, 0, 0),
                thickness + _THICK_OUTLINE, cv2.LINE_AA)
    cv2.putText(img, label, pos, _FONT, scale, color, thickness, cv2.LINE_AA)


# ── Raw-camera panel helpers ───────────────────────────────────────────────

def _draw_marker_overlay(
    raw: np.ndarray,
    centroids: List[Tuple[float, float]],
) -> np.ndarray:
    """
    Draw crosshairs + circles at detected alignment marker centroids and
    a quad connecting them if all 4 are found.
    """
    out = raw.copy()
    MAGENTA = (255, 0, 255)
    YELLOW  = (0, 255, 255)
    for cx, cy in centroids:
        ix, iy = int(cx), int(cy)
        cv2.circle(out, (ix, iy), 20, MAGENTA, 2, cv2.LINE_AA)
        cv2.line(out, (ix - 30, iy), (ix + 30, iy), MAGENTA, 1, cv2.LINE_AA)
        cv2.line(out, (ix, iy - 30), (ix, iy + 30), MAGENTA, 1, cv2.LINE_AA)

    # Connecting quad
    if len(centroids) == 4:
        pts = np.array([[int(x), int(y)] for x, y in centroids], dtype=np.int32)
        cv2.polylines(out, [pts], isClosed=True, color=YELLOW, thickness=1,
                      lineType=cv2.LINE_AA)

    # Label count
    found = len(centroids)
    label_color = (0, 200, 0) if found == 4 else (0, 80, 255)
    _text(out, f"Markers: {found}/4", (10, 28), 0.65, label_color, 2)
    return out


def _draw_hsv_mask(
    raw: np.ndarray,
    alignment_cfg,
) -> np.ndarray:
    """Tint pixels inside the marker HSV range bright magenta."""
    hsv = cv2.cvtColor(raw, cv2.COLOR_BGR2HSV)
    lo  = np.array([alignment_cfg.h_min, alignment_cfg.s_min, alignment_cfg.v_min],
                   dtype=np.uint8)
    hi  = np.array([alignment_cfg.h_max, alignment_cfg.s_max, alignment_cfg.v_max],
                   dtype=np.uint8)
    mask = cv2.inRange(hsv, lo, hi)
    out  = raw.copy()
    out[mask > 0] = (255, 50, 255)  # magenta tint
    return out


# ── Canonical panel helpers ────────────────────────────────────────────────

def _draw_canonical_overlay(
    canonical: np.ndarray,
    state,
    vertex_pos: List[Tuple[float, float]],
    edge_midpoints: List[Tuple[float, float]],
) -> np.ndarray:
    """
    Draw vertex and edge detection results on the canonical board image.

    Empty slots are shown as small gray outlines.  Occupied slots are filled
    circles/dots in the player's color with a black outline.
    """
    out = canonical.copy()

    # All vertex positions as gray outlines.
    for vx, vy in vertex_pos:
        cv2.circle(out, (int(vx), int(vy)), 7, (70, 70, 70), 1, cv2.LINE_AA)

    # All edge midpoints as tiny gray dots.
    for ex, ey in edge_midpoints:
        cv2.circle(out, (int(ex), int(ey)), 3, (70, 70, 70), 1, cv2.LINE_AA)

    # Occupied vertices.
    if state is not None:
        for vid, player in state.vertices.items():
            if player is None:
                continue
            vx, vy = vertex_pos[vid]
            color = _PLAYER_BGR[player % len(_PLAYER_BGR)]
            cv2.circle(out, (int(vx), int(vy)), 11, color, -1, cv2.LINE_AA)
            cv2.circle(out, (int(vx), int(vy)), 11, (0, 0, 0), 1, cv2.LINE_AA)

        # Occupied edges.
        for eid, player in state.edges.items():
            if player is None:
                continue
            ex, ey = edge_midpoints[eid]
            color = _PLAYER_BGR[player % len(_PLAYER_BGR)]
            cv2.circle(out, (int(ex), int(ey)), 6, color, -1, cv2.LINE_AA)
            cv2.circle(out, (int(ex), int(ey)), 6, (0, 0, 0), 1, cv2.LINE_AA)

    return out


# ── Status bar ─────────────────────────────────────────────────────────────

def _build_status_bar(
    fps: float,
    detect_ms: float,
    alignment_ok: bool,
    serial_ok: bool,
    no_serial: bool,
    frames_sent: int,
    state,
    error_msg: Optional[str],
    paused: bool,
) -> np.ndarray:
    bar = np.full((STATUS_H, DISPLAY_W, 3), (25, 25, 25), dtype=np.uint8)

    x = 12
    y_top = 28
    y_bot = 56

    # FPS + detect time
    fps_color = (60, 220, 60) if fps > 1.5 else (60, 180, 220)
    _text(bar, f"FPS {fps:4.1f}", (x, y_top), 0.55, fps_color, 1)
    _text(bar, f"det {detect_ms:5.0f}ms", (x, y_bot), 0.45, (160, 160, 160), 1)
    x += 130

    # Separator
    cv2.line(bar, (x, 8), (x, STATUS_H - 8), (60, 60, 60), 1)
    x += 10

    # Alignment
    al_color = (60, 220, 60) if alignment_ok else (30, 60, 230)
    al_label = "ALIGN  OK" if alignment_ok else "ALIGN FAIL"
    _text(bar, al_label, (x, y_top), 0.55, al_color, 1)
    x += 150

    # Separator
    cv2.line(bar, (x, 8), (x, STATUS_H - 8), (60, 60, 60), 1)
    x += 10

    # Serial
    if no_serial:
        _text(bar, "SERIAL  off", (x, y_top), 0.55, (110, 110, 110), 1)
    elif serial_ok:
        _text(bar, f"SERIAL TX {frames_sent}", (x, y_top), 0.55, (60, 220, 60), 1)
    else:
        _text(bar, "SERIAL ERR", (x, y_top), 0.55, (30, 60, 230), 1)
    x += 175

    # Separator
    cv2.line(bar, (x, 8), (x, STATUS_H - 8), (60, 60, 60), 1)
    x += 10

    # Per-player piece counts
    counts: Dict[int, int] = {p: 0 for p in range(4)}
    if state is not None:
        for v in state.vertices.values():
            if v is not None:
                counts[v] = counts.get(v, 0) + 1
        for e in state.edges.values():
            if e is not None:
                counts[e] = counts.get(e, 0) + 1

    for pid in range(4):
        c = counts.get(pid, 0)
        pcolor = _PLAYER_BGR[pid]
        label = f"{_PLAYER_NAMES[pid][0]}: {c}"   # "R: 5"
        cv2.circle(bar, (x + 8, y_top - 4), 7, pcolor, -1, cv2.LINE_AA)
        cv2.circle(bar, (x + 8, y_top - 4), 7, (0, 0, 0), 1, cv2.LINE_AA)
        _text(bar, label, (x + 20, y_top), 0.55, pcolor, 1)
        x += 100

    # Separator
    cv2.line(bar, (x, 8), (x, STATUS_H - 8), (60, 60, 60), 1)
    x += 10

    # Error / pause message
    if paused:
        _text(bar, "PAUSED [p]", (x, y_top), 0.55, (0, 200, 220), 1)
    elif error_msg:
        _text(bar, error_msg[:55], (x, y_top), 0.45, (30, 80, 240), 1)

    # Right-side: keyboard hints
    _text(bar, "q=quit  m=mask  p=pause", (DISPLAY_W - 240, STATUS_H - 12),
          0.4, (90, 90, 90), 1)

    return bar


# ── Composite renderer ─────────────────────────────────────────────────────

def _render(
    win_name: str,
    raw: Optional[np.ndarray],
    canonical: Optional[np.ndarray],
    state,
    centroids: List[Tuple[float, float]],
    vertex_pos: List[Tuple[float, float]],
    edge_midpoints: List[Tuple[float, float]],
    fps: float,
    detect_ms: float,
    frames_sent: int,
    alignment_ok: bool,
    serial_ok: bool,
    no_serial: bool,
    error_msg: Optional[str],
    show_mask: bool,
    paused: bool,
    alignment_cfg,
) -> None:
    # ── Left panel: raw camera ─────────────────────────────────────────────
    if raw is not None:
        left = raw.copy()
        if show_mask:
            left = _draw_hsv_mask(left, alignment_cfg)
        left = _draw_marker_overlay(left, centroids)
        left = cv2.resize(left, (PANEL_SIZE, PANEL_SIZE))
    else:
        left = np.zeros((PANEL_SIZE, PANEL_SIZE, 3), dtype=np.uint8)
        _text(left, "No camera frame", (PANEL_SIZE // 2 - 100, PANEL_SIZE // 2),
              0.7, (100, 100, 100), 1)

    # Label panel
    _text(left, "RAW  [m=mask]", (10, PANEL_SIZE - 12), 0.45, (100, 100, 100), 1)

    # ── Right panel: canonical board ───────────────────────────────────────
    if canonical is not None:
        right = _draw_canonical_overlay(canonical, state, vertex_pos, edge_midpoints)
        right = cv2.resize(right, (PANEL_SIZE, PANEL_SIZE))
    else:
        right = np.zeros((PANEL_SIZE, PANEL_SIZE, 3), dtype=np.uint8)
        msg = "Alignment failed — board not visible" if not alignment_ok \
              else "Waiting for first frame…"
        _text(right, msg, (20, PANEL_SIZE // 2), 0.55, (30, 60, 200), 1)

    _text(right, "CANONICAL", (10, PANEL_SIZE - 12), 0.45, (100, 100, 100), 1)

    # ── Status bar ─────────────────────────────────────────────────────────
    status = _build_status_bar(
        fps, detect_ms, alignment_ok, serial_ok, no_serial,
        frames_sent, state, error_msg, paused,
    )

    # ── Compose and show ───────────────────────────────────────────────────
    top_row = np.hstack([left, right])
    frame   = np.vstack([top_row, status])
    cv2.imshow(win_name, frame)


# ── CLI ────────────────────────────────────────────────────────────────────

def _parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="python -m vision.pi_debug",
        description="Catan CV debug UI — live camera and board-state display.",
    )
    p.add_argument("--port",       default="/dev/ttyAMA0",
                   help="Serial port to Arduino (default: %(default)s)")
    p.add_argument("--baud",       type=int, default=115200,
                   help="UART baud rate (default: %(default)s)")
    p.add_argument("--interval",   type=float, default=0.5,
                   help="Seconds between detection frames (default: %(default)s)")
    p.add_argument("--camera",     type=int, default=0,
                   help="Camera index (default: %(default)s)")
    p.add_argument("--no-alignment", dest="no_alignment", action="store_true",
                   help="Skip homography (camera is already in canonical view)")
    p.add_argument("--no-serial",    dest="no_serial", action="store_true",
                   help="Disable UART TX — display-only mode")
    p.add_argument("--fullscreen",   action="store_true",
                   help="Open window in fullscreen mode (for Pi HDMI display)")
    p.add_argument("--simulate",     metavar="IMAGE",
                   help="Use a static image file instead of the live camera")
    p.add_argument("--verbose",      action="store_true",
                   help="Enable DEBUG-level logging")
    return p.parse_args(argv)


# ── Main ───────────────────────────────────────────────────────────────────

def main(argv=None) -> int:  # noqa: C901  (deliberately monolithic for Pi deployment)
    args = _parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
        datefmt="%H:%M:%S",
    )

    # ── Vision imports ────────────────────────────────────────────────────
    try:
        from vision import BoardDetector, DetectorConfig, VERTEX_POS, EDGE_MIDPOINT
        from vision.alignment import (
            AlignmentConfig, AlignmentError,
            _detect_marker_centroids, find_homography, warp_to_canonical,
        )
        from vision.pi_serial import CameraCapture, build_cv_frame
    except ImportError as exc:
        LOG.error("Cannot import vision package: %s", exc)
        LOG.error("Run from the project root: python -m vision.pi_debug")
        return 1

    alignment_cfg = AlignmentConfig()

    # Build a detector that operates on already-warped canonical images so we
    # can perform alignment ourselves (to grab centroids for the overlay).
    skip_cfg  = DetectorConfig(skip_alignment=True)
    detector  = BoardDetector(skip_cfg)

    vertex_pos    = list(VERTEX_POS)
    edge_midpoints = list(EDGE_MIDPOINT)

    # ── Serial ────────────────────────────────────────────────────────────
    ser       = None
    serial_ok = False
    if not args.no_serial:
        try:
            import serial  # type: ignore
            ser = serial.Serial(args.port, args.baud, timeout=0.1)
            serial_ok = True
            LOG.info("Serial: %s @ %d baud", args.port, args.baud)
        except Exception as exc:
            LOG.warning("Serial unavailable (%s) — running display-only", exc)
            args.no_serial = True

    # ── Camera / simulate ─────────────────────────────────────────────────
    cam          = None
    static_image = None
    if args.simulate:
        static_image = cv2.imread(args.simulate)
        if static_image is None:
            LOG.error("Cannot read image: %s", args.simulate)
            return 1
        LOG.info("Simulate mode: %s", args.simulate)
    else:
        try:
            cam = CameraCapture(args.camera)
        except RuntimeError as exc:
            LOG.error("Camera init failed: %s", exc)
            if ser:
                ser.close()
            return 1

    # ── OpenCV window ─────────────────────────────────────────────────────
    WIN = "Catan CV Debug"
    cv2.namedWindow(WIN, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WIN, DISPLAY_W, DISPLAY_H)
    if args.fullscreen:
        cv2.setWindowProperty(WIN, cv2.WND_PROP_FULLSCREEN,
                              cv2.WINDOW_FULLSCREEN)

    # ── Loop state ────────────────────────────────────────────────────────
    raw: Optional[np.ndarray]       = None
    canonical: Optional[np.ndarray] = None
    centroids: List                 = []
    state                           = None
    fps        = 0.0
    detect_ms  = 0.0
    frames_sent   = 0
    alignment_ok  = False
    error_msg: Optional[str] = None
    show_mask  = False
    paused     = False
    last_detect_t = 0.0

    LOG.info("Debug UI started — q/Esc=quit  m=mask  p=pause")

    try:
        while True:
            # ── Key handling ──────────────────────────────────────────────
            key = cv2.waitKey(1) & 0xFF
            if key in (ord('q'), 27):
                break
            if key == ord('m'):
                show_mask = not show_mask
            if key == ord('p'):
                paused = not paused
                LOG.info("Detection %s", "paused" if paused else "resumed")

            # Check if window was closed by the user
            if cv2.getWindowProperty(WIN, cv2.WND_PROP_VISIBLE) < 1:
                break

            now = time.monotonic()

            # ── Detection (throttled) ─────────────────────────────────────
            if not paused and (now - last_detect_t) >= args.interval:
                last_detect_t = now

                # Capture
                if static_image is not None:
                    raw = static_image.copy()
                else:
                    try:
                        raw = cam.capture()
                    except RuntimeError as exc:
                        error_msg = f"Capture error: {exc}"
                        LOG.warning(error_msg)
                        raw = None

                if raw is not None:
                    t0 = time.monotonic()

                    # Marker detection (for overlay — separate from alignment)
                    centroids = []
                    if not args.no_alignment:
                        try:
                            centroids = _detect_marker_centroids(raw, alignment_cfg)
                        except Exception as exc:
                            LOG.debug("Marker detection error: %s", exc)

                    # Alignment + warp
                    try:
                        if args.no_alignment:
                            canonical = raw.copy()
                        else:
                            H = find_homography(raw, alignment_cfg)
                            canonical = warp_to_canonical(raw, H)

                        state        = detector.detect(canonical)
                        alignment_ok = True
                        error_msg    = None

                    except AlignmentError as exc:
                        alignment_ok = False
                        error_msg    = f"Align: {exc}"
                        LOG.debug("Alignment failed: %s", exc)

                    except Exception as exc:
                        alignment_ok = False
                        error_msg    = f"Error: {exc}"
                        LOG.exception("Detector error")

                    detect_ms = (time.monotonic() - t0) * 1000.0
                    fps       = 1000.0 / max(detect_ms, 1.0)

                    # Serial TX
                    if ser is not None and state is not None and alignment_ok:
                        try:
                            frame_bytes = build_cv_frame(
                                state.vertices, state.edges
                            )
                            ser.write(frame_bytes)
                            frames_sent += 1
                            serial_ok = True
                        except Exception as exc:
                            serial_ok = False
                            LOG.warning("Serial write error: %s", exc)

                    occupied_v = sum(
                        1 for v in state.vertices.values() if v is not None
                    ) if state else 0
                    occupied_e = sum(
                        1 for e in state.edges.values() if e is not None
                    ) if state else 0
                    LOG.debug(
                        "det=%.0fms  align=%s  V=%d E=%d  tx=%d",
                        detect_ms, alignment_ok, occupied_v, occupied_e,
                        frames_sent,
                    )

            # ── Render ────────────────────────────────────────────────────
            _render(
                WIN, raw, canonical, state, centroids,
                vertex_pos, edge_midpoints,
                fps, detect_ms, frames_sent,
                alignment_ok, serial_ok, args.no_serial,
                error_msg, show_mask, paused, alignment_cfg,
            )

            # Small sleep to cap render loop at ~60fps without burning CPU.
            loop_elapsed = time.monotonic() - now
            time.sleep(max(0.0, 0.016 - loop_elapsed))

    except KeyboardInterrupt:
        LOG.info("Stopped by user (tx=%d)", frames_sent)
    finally:
        cv2.destroyAllWindows()
        if cam:
            cam.close()
        if ser:
            ser.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
