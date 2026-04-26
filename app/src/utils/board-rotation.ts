// =============================================================================
// board-rotation.ts — Board orientation utilities for the mobile client.
//
// The physical Catan board has a reference "front" orientation. Players
// sitting at different sides of the table need to rotate the board view on
// their device so the board renders from their perspective.
//
// Rotation is expressed in steps of 60° clockwise as seen on screen.
//
// Orientation pattern (shown on physical LEDs in lobby):
//   Tiles T15 (0,-2), T16 (1,-2), T17 (2,-2)  →  bright-white strip (top edge)
//   Tile  T18 (2,-1)                            →  golden accent
// The strip + accent form an asymmetric "L" that is unambiguous at all 6
// rotations. Players rotate the mobile view until the highlighted tiles
// match what they see physically.
// =============================================================================

/** Valid board rotation steps (0 = default, each step = 60° CW on screen). */
export type BoardRotation = 0 | 1 | 2 | 3 | 4 | 5;

/** Three tile indices and their colors that form the orientation triangle. */
export const ORIENTATION_POINTS: ReadonlyArray<{ tile: number; color: string }> = [
  { tile: 14, color: '#dc0000' },  // red
  { tile: 18, color: '#00c800' },  // green
  { tile: 10, color: '#0000dc' },  // blue
];

/**
 * Rotate an axial hex coordinate (q, r) by `steps` × 60°.
 * Each step applies: (q, r) → (q+r, -q).
 * Note: in pixel space (y-down) this results in a CCW visual rotation.
 * Use `rotatePoint` for pixel-space CW rotation.
 */
export function rotateHexCoord(
  q: number,
  r: number,
  steps: BoardRotation,
): { q: number; r: number } {
  let cq = q, cr = r;
  const n = ((steps % 6) + 6) % 6;
  for (let i = 0; i < n; i++) {
    const nq = cq + cr;
    cr = -cq;
    cq = nq;
  }
  return { q: cq, r: cr };
}

/**
 * Rotate a 2-D pixel point by `steps` × 60° **clockwise as seen on screen**
 * (screen coordinate system: x right, y down).
 *
 * CW rotation in screen space uses the standard rotation matrix with the
 * y-axis inverted:
 *   x' = x·cos θ − y·sin θ
 *   y' = x·sin θ + y·cos θ
 */
export function rotatePoint(
  x: number,
  y: number,
  steps: BoardRotation,
): { x: number; y: number } {
  if (steps === 0) return { x, y };
  const angle = (steps * 60 * Math.PI) / 180;
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return { x: x * c - y * s, y: x * s + y * c };
}
