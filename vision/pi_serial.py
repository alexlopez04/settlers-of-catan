#!/usr/bin/env python3
"""pi_serial.py — Raspberry Pi side of the CV-to-Mega serial link.

Captures a top-down image of the Catan board, runs BoardDetector, and sends
a CATAN_MSG_CV_BOARD_STATE frame over UART to the Arduino Mega (Serial2).

Frame format (CATAN_WIRE protocol):
  [0xCA magic][type:u8][len:u8][payload:63 bytes][crc8:u8]

Payload (63 bytes, nibble-packed):
  Bytes  0-26: 54 vertex owners  (2 vertices per byte, low nibble first)
  Bytes 27-62: 72 edge owners    (2 edges  per byte, low nibble first)
  Nibble value 0-3 = player index, 0xF = empty/unoccupied

Usage:
  python -m vision.pi_serial
  python -m vision.pi_serial --port /dev/ttyAMA0 --baud 115200 --interval 0.5
  python -m vision.pi_serial --no-alignment   # skip homography (fixed camera)
  python -m vision.pi_serial --simulate <image.jpg>  # single-image dry-run

Hardware note:
  Mega Serial2: TX2=16 / RX2=17 at 115200 baud.
  Requires a 3.3 V ↔ 5 V level shifter between Pi GPIO UART and Mega Serial2.
  Default Pi port: /dev/ttyAMA0 (GPIO14=TX, GPIO15=RX, enable_uart=1 in
  /boot/config.txt and remove 'console=serial0' from /boot/cmdline.txt).
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
import time
from typing import Dict, Optional

# ── CATAN_WIRE constants (must match firmware/board/src/catan_wire.h) ─────────

CATAN_WIRE_MAGIC          = 0xCA
CATAN_MSG_CV_BOARD_STATE  = 0x04
CATAN_CV_VERTEX_BYTES     = 27   # ceil(54 / 2)
CATAN_CV_EDGE_BYTES       = 36   # ceil(72 / 2)
CATAN_CV_PAYLOAD_SIZE     = 63   # VERTEX_BYTES + EDGE_BYTES
NUM_VERTICES              = 54
NUM_EDGES                 = 72

LOG = logging.getLogger("pi_serial")

# ── CRC-8 (poly=0x07, init=0x00) ─────────────────────────────────────────────

def crc8(data: bytes) -> int:
    """Compute CRC-8 (CATAN_WIRE variant) over *data*."""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

# ── Frame builder ─────────────────────────────────────────────────────────────

def build_cv_frame(
    vertex_owners: Dict[int, Optional[int]],
    edge_owners: Dict[int, Optional[int]],
) -> bytes:
    """Pack ownership dicts into a CATAN_WIRE CATAN_MSG_CV_BOARD_STATE frame.

    Args:
        vertex_owners: mapping vertex_id → player_index (0-3) or None.
        edge_owners:   mapping edge_id   → player_index (0-3) or None.

    Returns:
        5 + CATAN_CV_PAYLOAD_SIZE bytes: magic, type, len, payload, crc8.
    """
    payload = bytearray(CATAN_CV_PAYLOAD_SIZE)

    # Initialise all nibbles to 0xF (empty).
    for i in range(CATAN_CV_PAYLOAD_SIZE):
        payload[i] = 0xFF

    # Encode vertices (bytes 0-26).
    for vid in range(NUM_VERTICES):
        owner = vertex_owners.get(vid)
        nib = (owner & 0x0F) if owner is not None else 0x0F
        byte_idx = vid >> 1
        if vid & 1:
            payload[byte_idx] = (payload[byte_idx] & 0x0F) | (nib << 4)
        else:
            payload[byte_idx] = (payload[byte_idx] & 0xF0) | nib

    # Encode edges (bytes 27-62).
    for eid in range(NUM_EDGES):
        owner = edge_owners.get(eid)
        nib = (owner & 0x0F) if owner is not None else 0x0F
        byte_idx = (eid >> 1) + CATAN_CV_VERTEX_BYTES
        if eid & 1:
            payload[byte_idx] = (payload[byte_idx] & 0x0F) | (nib << 4)
        else:
            payload[byte_idx] = (payload[byte_idx] & 0xF0) | nib

    # Build frame: [magic][type][len][payload][crc8].
    # CRC covers [type, len, payload].
    crc_data = bytes([CATAN_MSG_CV_BOARD_STATE, CATAN_CV_PAYLOAD_SIZE]) + bytes(payload)
    frame = bytes([
        CATAN_WIRE_MAGIC,
        CATAN_MSG_CV_BOARD_STATE,
        CATAN_CV_PAYLOAD_SIZE,
    ]) + bytes(payload) + bytes([crc8(crc_data)])
    return frame

# ── Camera wrapper ─────────────────────────────────────────────────────────────

class CameraCapture:
    """Thin wrapper that tries picamera2 first, then falls back to cv2."""

    def __init__(self, camera_index: int = 0) -> None:
        self._cam = None
        self._use_picamera2 = False
        try:
            from picamera2 import Picamera2  # type: ignore
            self._cam = Picamera2(camera_index)
            config = self._cam.create_still_configuration(
                main={"size": (1280, 960), "format": "BGR888"}
            )
            self._cam.configure(config)
            self._cam.start()
            self._use_picamera2 = True
            LOG.info("Camera: using picamera2 (index %d)", camera_index)
        except Exception as exc:
            LOG.debug("picamera2 unavailable (%s); falling back to cv2", exc)
            import cv2  # type: ignore
            cap = cv2.VideoCapture(camera_index)
            if not cap.isOpened():
                raise RuntimeError(
                    f"Cannot open camera index {camera_index} via cv2"
                ) from exc
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,  1280)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT,  960)
            self._cam = cap
            LOG.info("Camera: using cv2.VideoCapture (index %d)", camera_index)

    def capture(self):
        """Return a BGR numpy array (H×W×3), or raise RuntimeError."""
        if self._use_picamera2:
            import numpy as np  # type: ignore
            return self._cam.capture_array()  # already BGR888
        else:
            import cv2  # type: ignore
            ret, frame = self._cam.read()
            if not ret or frame is None:
                raise RuntimeError("cv2.VideoCapture.read() failed")
            return frame

    def close(self) -> None:
        if self._cam is None:
            return
        try:
            if self._use_picamera2:
                self._cam.stop()
                self._cam.close()
            else:
                self._cam.release()
        except Exception:
            pass
        self._cam = None

    def __del__(self) -> None:
        self.close()

# ── Main loop ─────────────────────────────────────────────────────────────────

def _parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Stream Catan board state from Pi camera to Arduino Mega."
    )
    p.add_argument("--port",     default="/dev/ttyAMA0",
                   help="Serial port connected to Mega Serial2 (default: %(default)s)")
    p.add_argument("--baud",     type=int, default=115200,
                   help="Baud rate (default: %(default)s)")
    p.add_argument("--interval", type=float, default=0.5,
                   help="Target seconds between frames sent (default: %(default)s)")
    p.add_argument("--camera",   type=int, default=0,
                   help="Camera index (default: %(default)s)")
    p.add_argument("--no-alignment", action="store_true",
                   help="Skip homography alignment (fixed/calibrated camera)")
    p.add_argument("--simulate", metavar="IMAGE",
                   help="Single-image dry-run: detect and print, then exit")
    p.add_argument("--verbose",  action="store_true",
                   help="Enable DEBUG logging")
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = _parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
        datefmt="%H:%M:%S",
    )

    # Import vision package.
    try:
        from vision import BoardDetector, DetectorConfig
        from vision.alignment import AlignmentError
    except ImportError as exc:
        LOG.error("Cannot import vision package: %s", exc)
        LOG.error("Run from the project root with: python -m vision.pi_serial")
        return 1

    cfg = DetectorConfig(skip_alignment=args.no_alignment)
    detector = BoardDetector(cfg)

    # ── Simulate mode ────────────────────────────────────────────────────────
    if args.simulate:
        import cv2  # type: ignore
        img = cv2.imread(args.simulate)
        if img is None:
            LOG.error("Cannot read image: %s", args.simulate)
            return 1
        try:
            state = detector.detect(img)
        except AlignmentError as exc:
            LOG.error("Alignment failed: %s", exc)
            return 1

        occupied_v = {k: v for k, v in state.vertices.items() if v is not None}
        occupied_e = {k: v for k, v in state.edges.items()    if v is not None}
        print(f"Vertices ({len(occupied_v)} occupied): {occupied_v}")
        print(f"Edges    ({len(occupied_e)} occupied): {occupied_e}")

        frame = build_cv_frame(state.vertices, state.edges)
        print(f"Frame ({len(frame)} bytes): {frame.hex()}")
        return 0

    # ── Live mode ─────────────────────────────────────────────────────────────
    try:
        import serial  # type: ignore
    except ImportError:
        LOG.error("pyserial is not installed. Run: pip install pyserial")
        return 1

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
        LOG.info("Serial: %s at %d baud", args.port, args.baud)
    except serial.SerialException as exc:
        LOG.error("Cannot open serial port %s: %s", args.port, exc)
        return 1

    try:
        cam = CameraCapture(args.camera)
    except RuntimeError as exc:
        LOG.error("Camera init failed: %s", exc)
        ser.close()
        return 1

    LOG.info("Starting capture loop (interval=%.2fs, alignment=%s)",
             args.interval, "off" if args.no_alignment else "on")

    frames_sent = 0
    frames_skipped = 0

    try:
        while True:
            t0 = time.monotonic()

            try:
                image = cam.capture()
            except RuntimeError as exc:
                LOG.warning("Capture error: %s — skipping frame", exc)
                frames_skipped += 1
                time.sleep(args.interval)
                continue

            try:
                state = detector.detect(image)
            except AlignmentError as exc:
                LOG.warning("Alignment failed: %s — skipping frame", exc)
                frames_skipped += 1
                time.sleep(max(0.0, args.interval - (time.monotonic() - t0)))
                continue
            except Exception as exc:
                LOG.error("Detector error: %s — skipping frame", exc)
                frames_skipped += 1
                time.sleep(max(0.0, args.interval - (time.monotonic() - t0)))
                continue

            frame = build_cv_frame(state.vertices, state.edges)
            try:
                ser.write(frame)
                frames_sent += 1
            except serial.SerialException as exc:
                LOG.error("Serial write error: %s", exc)
                break

            occupied = sum(1 for v in state.vertices.values() if v is not None)
            occupied += sum(1 for v in state.edges.values()    if v is not None)
            LOG.debug("Frame %d: %d pieces occupied, %d bytes sent",
                      frames_sent, occupied, len(frame))

            elapsed = time.monotonic() - t0
            remaining = args.interval - elapsed
            if remaining > 0:
                time.sleep(remaining)

    except KeyboardInterrupt:
        LOG.info("Stopped by user (sent=%d skipped=%d)", frames_sent, frames_skipped)
    finally:
        cam.close()
        ser.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
