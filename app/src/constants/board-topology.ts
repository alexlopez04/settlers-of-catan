// =============================================================================
// board-topology.ts — Static Catan board graph data + geometry helpers.
//
// Mirrors firmware/src/board_topology.cpp (tile / vertex / edge / port
// topology) and the hex layout in docs/board_layout.md. Keep this file in
// sync with the firmware tables — positions are derived from them.
//
// Coordinate system: pointy-top axial hex (q, r).
//   Tile center in unit-hex space (radius = 1):
//     x = sqrt(3) * (q + r/2)
//     y = 1.5 * r
//   Tile corners, clockwise from North (matches firmware ordering):
//     0=N  1=NE  2=SE  3=S  4=SW  5=NW
//   Tile edges, clockwise from the NE edge (between corners 0 and 1):
//     0=NE  1=E  2=SE  3=SW  4=W  5=NW
// =============================================================================

import { Biome } from '@/services/proto';

export const TILE_COUNT   = 19;
export const VERTEX_COUNT = 54;
export const EDGE_COUNT   = 72;
export const PORT_COUNT   = 9;

/** Axial hex coordinates for each tile id (T00..T18). */
export const TILE_HEX_COORDS: ReadonlyArray<{ q: number; r: number }> = [
  { q:  0, r:  0 }, // T00 centre
  { q:  1, r:  0 }, // T01
  { q:  0, r:  1 }, // T02
  { q: -1, r:  1 }, // T03
  { q: -1, r:  0 }, // T04
  { q:  0, r: -1 }, // T05
  { q:  1, r: -1 }, // T06
  { q:  2, r:  0 }, // T07
  { q:  1, r:  1 }, // T08
  { q:  0, r:  2 }, // T09
  { q: -1, r:  2 }, // T10
  { q: -2, r:  2 }, // T11
  { q: -2, r:  1 }, // T12
  { q: -2, r:  0 }, // T13
  { q: -1, r: -1 }, // T14
  { q:  0, r: -2 }, // T15
  { q:  1, r: -2 }, // T16
  { q:  2, r: -2 }, // T17
  { q:  2, r: -1 }, // T18
];

/** For each tile, the 6 vertex ids in corner order (S, SE, NE, N, NW, SW) — CW from S in screen coords. */
// Note: this is NOT CW-from-N; use VERTEX_POS_TO_CORNER to map positions to tileCorner indices.
export const TILE_VERTICES: ReadonlyArray<ReadonlyArray<number>> = [
  /* T00 */ [ 0,  1,  2,  3,  4,  5],
  /* T01 */ [ 6,  7,  8,  9,  2,  1],
  /* T02 */ [10, 11,  6,  1,  0, 12],
  /* T03 */ [13, 12,  0,  5, 14, 15],
  /* T04 */ [14,  5,  4, 16, 17, 18],
  /* T05 */ [ 4,  3, 19, 20, 21, 16],
  /* T06 */ [ 2,  9, 22, 23, 19,  3],
  /* T07 */ [24, 25, 26, 27,  8,  7],
  /* T08 */ [28, 29, 24,  7,  6, 11],
  /* T09 */ [30, 31, 28, 11, 10, 32],
  /* T10 */ [33, 32, 10, 12, 13, 34],
  /* T11 */ [35, 34, 13, 15, 36, 37],
  /* T12 */ [36, 15, 14, 18, 38, 39],
  /* T13 */ [38, 18, 17, 40, 41, 42],
  /* T14 */ [17, 16, 21, 43, 44, 40],
  /* T15 */ [21, 20, 45, 46, 47, 43],
  /* T16 */ [19, 23, 48, 49, 45, 20],
  /* T17 */ [22, 50, 51, 52, 48, 23],
  /* T18 */ [ 8, 27, 53, 50, 22,  9],
];

/** For each tile, the 6 edge ids in edge order (SE, E, NE, NW, W, SW) — matching TILE_VERTICES CW-from-S ordering. */
export const TILE_EDGES: ReadonlyArray<ReadonlyArray<number>> = [
  /* T00 */ [ 0,  1,  2,  3,  4,  5],
  /* T01 */ [ 6,  7,  8,  9,  1, 10],
  /* T02 */ [11, 12, 10,  0, 13, 14],
  /* T03 */ [15, 13,  5, 16, 17, 18],
  /* T04 */ [16,  4, 19, 20, 21, 22],
  /* T05 */ [ 3, 23, 24, 25, 26, 19],
  /* T06 */ [ 9, 27, 28, 29, 23,  2],
  /* T07 */ [30, 31, 32, 33,  7, 34],
  /* T08 */ [35, 36, 34,  6, 12, 37],
  /* T09 */ [38, 39, 37, 11, 40, 41],
  /* T10 */ [42, 40, 14, 15, 43, 44],
  /* T11 */ [45, 43, 18, 46, 47, 48],
  /* T12 */ [46, 17, 22, 49, 50, 51],
  /* T13 */ [49, 21, 52, 53, 54, 55],
  /* T14 */ [20, 26, 56, 57, 58, 52],
  /* T15 */ [25, 59, 60, 61, 62, 56],
  /* T16 */ [29, 63, 64, 65, 59, 24],
  /* T17 */ [66, 67, 68, 69, 63, 28],
  /* T18 */ [33, 70, 71, 66, 27,  8],
];

/** Vertex endpoints for each edge. Matches firmware EDGE_TOPO. */
export const EDGE_VERTICES: ReadonlyArray<readonly [number, number]> = [
  [ 0,  1], [ 1,  2], [ 2,  3], [ 3,  4], [ 4,  5], [ 0,  5],   // E00–E05
  [ 6,  7], [ 7,  8], [ 8,  9], [ 2,  9], [ 1,  6],             // E06–E10
  [10, 11], [ 6, 11], [ 0, 12], [10, 12], [12, 13],             // E11–E15
  [ 5, 14], [14, 15], [13, 15],                                 // E16–E18
  [ 4, 16], [16, 17], [17, 18], [14, 18],                       // E19–E22
  [ 3, 19], [19, 20], [20, 21], [16, 21],                       // E23–E26
  [ 9, 22], [22, 23], [19, 23],                                 // E27–E29
  [24, 25], [25, 26], [26, 27], [ 8, 27], [ 7, 24],             // E30–E34
  [28, 29], [24, 29], [11, 28],                                 // E35–E37
  [30, 31], [28, 31], [10, 32], [30, 32],                       // E38–E41
  [32, 33], [13, 34], [33, 34],                                 // E42–E44
  [34, 35], [15, 36], [36, 37], [35, 37],                       // E45–E48
  [18, 38], [38, 39], [36, 39],                                 // E49–E51
  [17, 40], [40, 41], [41, 42], [38, 42],                       // E52–E55
  [21, 43], [43, 44], [40, 44],                                 // E56–E58
  [20, 45], [45, 46], [46, 47], [43, 47],                       // E59–E62
  [23, 48], [48, 49], [45, 49],                                 // E63–E65
  [22, 50], [50, 51], [51, 52], [48, 52],                       // E66–E69
  [27, 53], [50, 53],                                           // E70–E71
];

// ── Ports ───────────────────────────────────────────────────────────────────

export enum PortType {
  GENERIC_3_1 = 0,
  LUMBER_2_1  = 1,
  WOOL_2_1    = 2,
  GRAIN_2_1   = 3,
  BRICK_2_1   = 4,
  ORE_2_1     = 5,
}

export interface PortDef {
  type: PortType;
  vertices: readonly [number, number];
}

/** Static port layout, matching firmware PORT_TOPO. */
export const PORTS: ReadonlyArray<PortDef> = [
  { type: PortType.GENERIC_3_1, vertices: [29, 28] }, // P0
  { type: PortType.LUMBER_2_1,  vertices: [25, 26] }, // P1
  { type: PortType.GENERIC_3_1, vertices: [50, 53] }, // P2
  { type: PortType.WOOL_2_1,    vertices: [48, 49] }, // P3
  { type: PortType.GENERIC_3_1, vertices: [46, 47] }, // P4
  { type: PortType.GRAIN_2_1,   vertices: [44, 40] }, // P5
  { type: PortType.BRICK_2_1,   vertices: [38, 39] }, // P6
  { type: PortType.GENERIC_3_1, vertices: [35, 37] }, // P7
  { type: PortType.ORE_2_1,     vertices: [32, 33] }, // P8
];

// ── Geometry ────────────────────────────────────────────────────────────────

export interface Point { x: number; y: number; }

const SQRT3 = Math.sqrt(3);

/** Axial (q, r) → pixel centre, for pointy-top hexes of radius `hexSize`. */
export function tileCenter(q: number, r: number, hexSize: number): Point {
  return {
    x: hexSize * SQRT3 * (q + r / 2),
    y: hexSize * 1.5 * r,
  };
}

/**
 * Corner position of a tile. `corner` is 0..5 in firmware CW-from-N order
 * (0=N, 1=NE, 2=SE, 3=S, 4=SW, 5=NW).
 */
export function tileCorner(centre: Point, corner: number, hexSize: number): Point {
  // Firmware N is straight up (screen y-axis points down, so angle = -90°).
  // CW order in screen coords means ANGLE INCREASES by 60° per step.
  const angleRad = ((-90 + corner * 60) * Math.PI) / 180;
  return {
    x: centre.x + hexSize * Math.cos(angleRad),
    y: centre.y + hexSize * Math.sin(angleRad),
  };
}

/** Six corner positions CW from N for the given tile centre. */
export function hexCorners(centre: Point, hexSize: number): Point[] {
  return Array.from({ length: 6 }, (_, i) => tileCorner(centre, i, hexSize));
}

/**
 * TILE_VERTICES stores vertex IDs in the order [S, SE, NE, N, NW, SW]
 * (starting from the S corner, going counter-clockwise in tileCorner's
 * 0=N CW numbering). tileCorner uses 0=N, 1=NE, 2=SE, 3=S, 4=SW, 5=NW.
 * So TILE_VERTICES position k maps to tileCorner index (9 - k) % 6:
 *   pos 0 (S)  → corner 3
 *   pos 1 (SE) → corner 2
 *   pos 2 (NE) → corner 1
 *   pos 3 (N)  → corner 0
 *   pos 4 (NW) → corner 5
 *   pos 5 (SW) → corner 4
 */
const VERTEX_POS_TO_CORNER = [3, 2, 1, 0, 5, 4] as const;

/**
 * Resolve canonical positions for all 54 vertices. Because the tile graph is
 * consistent, every (tile, position) that references a vertex yields the same
 * geometric point; later writes just overwrite with an identical value.
 */
export function computeVertexPositions(hexSize: number): Point[] {
  const out: Point[] = new Array(VERTEX_COUNT);
  for (let t = 0; t < TILE_COUNT; t++) {
    const { q, r } = TILE_HEX_COORDS[t];
    const c = tileCenter(q, r, hexSize);
    for (let k = 0; k < 6; k++) {
      const vid = TILE_VERTICES[t][k];
      out[vid] = tileCorner(c, VERTEX_POS_TO_CORNER[k], hexSize);
    }
  }
  return out;
}

// ── Ports: geometry helpers ────────────────────────────────────────────────

/**
 * Given a port (two coastal vertices), return the port anchor point pushed
 * outward from the board centre, plus the inward direction to draw the two
 * "dock lines" back to the coast. This makes ports render outside the
 * hex ring with a pair of lines touching the two claim-vertices.
 */
export function portAnchor(
  port: PortDef,
  vertexPositions: Point[],
  hexSize: number,
): { anchor: Point; a: Point; b: Point; } {
  const a = vertexPositions[port.vertices[0]];
  const b = vertexPositions[port.vertices[1]];
  const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
  // Outward direction = away from board centre (0, 0).
  const len = Math.hypot(mid.x, mid.y) || 1;
  const ox = mid.x / len;
  const oy = mid.y / len;
  const offset = hexSize * 0.9;
  return {
    anchor: { x: mid.x + ox * offset, y: mid.y + oy * offset },
    a,
    b,
  };
}
