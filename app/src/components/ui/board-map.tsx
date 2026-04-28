// =============================================================================
// board-map.tsx — Reusable SVG hex-grid board renderer.
//
// Used by:
//   - BoardOverview modal  (full game state, read-only)
//   - LobbyOrientationPicker (blank board + orientation pattern)
//   - RobberMapPicker  (full board, tappable tiles)
//
// Coordinate system:
//   - The SVG viewBox is centred at the board origin (tile T00 centre).
//   - hexSize = size / 12  (tiles are ~1/6 of the render width).
//   - All geometry is derived from board-topology helpers, then rotated in
//     pixel space by the player's boardRotation.
// =============================================================================

import React, { useMemo } from 'react';
import {
  Circle,
  G,
  Line,
  Polygon,
  Svg,
  Text as SvgText,
} from 'react-native-svg';

import {
  EDGE_VERTICES,
  PORTS,
  TILE_COUNT,
  TILE_HEX_COORDS,
  TILE_VERTICES,
  computeVertexPositions,
  portAnchor,
  tileCenter,
  PortType,
} from '@/constants/board-topology';
import { Biome, EdgeOwner, Tile, VertexOwner } from '@/services/proto';
import {
  BoardRotation,
  ORIENTATION_POINTS,
  rotatePoint,
} from '@/utils/board-rotation';

// ── Colour constants ─────────────────────────────────────────────────────────
// Exported so the BoardOverview legend can use identical colours.

export const BIOME_FILL: Record<Biome, string> = {
  [Biome.DESERT]:   '#c8b978',  // sandy beige
  [Biome.FOREST]:   '#3d5e35',  // dark muted green
  [Biome.PASTURE]:  '#6a9e52',  // medium muted green
  [Biome.FIELD]:    '#c09820',  // muted amber
  [Biome.HILL]:     '#9a4030',  // muted terracotta
  [Biome.MOUNTAIN]: '#6a5490',  // muted mauve
};

const BIOME_STROKE: Record<Biome, string> = {
  [Biome.DESERT]:   '#a89858',
  [Biome.FOREST]:   '#243e20',
  [Biome.PASTURE]:  '#4a7a38',
  [Biome.FIELD]:    '#8a7010',
  [Biome.HILL]:     '#6e2820',
  [Biome.MOUNTAIN]: '#4a3870',
};

const EMPTY_FILL   = '#3a4a5a';
const EMPTY_STROKE = '#2a3848';

/** Quick lookup: tile index → orientation color (undefined = not an orientation tile). */
const ORIENTATION_COLOR: Record<number, string> = Object.fromEntries(
  ORIENTATION_POINTS.map(p => [p.tile, p.color]),
);

export const PLAYER_FILL = ['#e63946', '#4361ee', '#f4a261', '#f0f0f0'];
const PLAYER_STROKE = ['#a01020', '#2040b0', '#c06820', '#909090'];

const PORT_FILL: Record<PortType, string> = {
  [PortType.GENERIC_3_1]: '#aaaaaa',
  [PortType.LUMBER_2_1]:  '#3d5e35',
  [PortType.WOOL_2_1]:    '#6a9e52',
  [PortType.GRAIN_2_1]:   '#c09820',
  [PortType.BRICK_2_1]:   '#9a4030',
  [PortType.ORE_2_1]:     '#6a5490',
};

/** Short resource label shown on each tile (inside the token for numbered tiles, centred for desert). */
const BIOME_LABEL: Record<Biome, string> = {
  [Biome.DESERT]:   'Desert',
  [Biome.FOREST]:   'Lumber',
  [Biome.PASTURE]:  'Wool',
  [Biome.FIELD]:    'Grain',
  [Biome.HILL]:     'Brick',
  [Biome.MOUNTAIN]: 'Ore',
};

/** Text colour that contrasts against each biome fill. */
const BIOME_LABEL_COLOR: Record<Biome, string> = {
  [Biome.DESERT]:   '#6a5830',
  [Biome.FOREST]:   'rgba(255,255,255,0.75)',
  [Biome.PASTURE]:  'rgba(255,255,255,0.80)',
  [Biome.FIELD]:    'rgba(255,255,255,0.80)',
  [Biome.HILL]:     'rgba(255,255,255,0.75)',
  [Biome.MOUNTAIN]: 'rgba(255,255,255,0.80)',
};

const PORT_LABELS: Record<PortType, string> = {
  [PortType.GENERIC_3_1]: '3:1',
  [PortType.LUMBER_2_1]:  '\u{1F332}',  // 🌲
  [PortType.WOOL_2_1]:    '\u{1F411}',  // 🐑
  [PortType.GRAIN_2_1]:   '\u{1F33E}',  // 🌾
  [PortType.BRICK_2_1]:   '\u{1F9F1}',  // 🧱
  [PortType.ORE_2_1]:     '\u26F0',     // ⛰
};

// ── Props ────────────────────────────────────────────────────────────────────

export interface BoardMapProps {
  /** Tile data from BoardState. Pass null/empty array for a blank board. */
  tiles: Tile[] | null | undefined;
  /** Vertex ownership data (length 54). */
  vertices?: (VertexOwner | null)[] | null;
  /** Edge ownership data (length 72). */
  edges?: (EdgeOwner | null)[] | null;
  /** Current robber tile index (dims that tile). */
  robberTile?: number;

  /** Player's board rotation (0–5 steps × 60° CW). */
  rotation: BoardRotation;
  /** Rendered pixel width & height (square). */
  size: number;

  /** Called when a tile is tapped (robber picker). */
  onTilePress?: (tileIndex: number) => void;
  /**
   * Tile index that is disabled / greyed in the picker (usually the current
   * robber tile so the player cannot re-select it).
   */
  disabledTile?: number;

  /** Overlay the lobby orientation-pattern highlight (strips + accent). */
  showOrientationPattern?: boolean;
  /** Render port indicators. Default true. */
  showPorts?: boolean;
  /** Debug: show vertex index labels. */
  showVertexIndices?: boolean;
  /** Debug: show edge index labels. */
  showEdgeIndices?: boolean;
}

// ── Component ────────────────────────────────────────────────────────────────

export function BoardMap({
  tiles,
  vertices,
  edges,
  robberTile,
  rotation,
  size,
  onTilePress,
  disabledTile,
  showOrientationPattern = false,
  showPorts = true,
  showVertexIndices = false,
  showEdgeIndices = false,
}: BoardMapProps) {
  // hexSize drives the scale of the entire board.
  // viewBox spans ±6·hs in each direction, total 12·hs.
  const hs = size / 12;

  // ── Geometry (memoised on size + rotation) ─────────────────────────────

  /** All 54 vertex positions, rotated into screen space. */
  const rotVerts = useMemo(() => {
    const orig = computeVertexPositions(hs);
    return orig.map(v =>
      v ? rotatePoint(v.x, v.y, rotation) : { x: 0, y: 0 },
    );
  }, [hs, rotation]);

  /** Tile centres in rotated screen space (for text/token placement). */
  const tileCentres = useMemo(() => {
    return TILE_HEX_COORDS.map(({ q, r }) => {
      const orig = tileCenter(q, r, hs);
      return rotatePoint(orig.x, orig.y, rotation);
    });
  }, [hs, rotation]);

  // ── Helpers ────────────────────────────────────────────────────────────

  const vbHalf = (hs * 12) / 2;

  function polygonPoints(tileIdx: number): string {
    return TILE_VERTICES[tileIdx]
      .map(vi => `${rotVerts[vi].x.toFixed(2)},${rotVerts[vi].y.toFixed(2)}`)
      .join(' ');
  }

  function tileFill(t: number): string {
    if (showOrientationPattern) {
      return ORIENTATION_COLOR[t] ?? EMPTY_FILL;
    }
    if (!tiles || tiles.length === 0 || t >= tiles.length) return EMPTY_FILL;
    return BIOME_FILL[tiles[t].biome];
  }

  function tileStroke(t: number): string {
    if (showOrientationPattern) return '#1a2530';
    if (!tiles || tiles.length === 0 || t >= tiles.length) return EMPTY_STROKE;
    return BIOME_STROKE[tiles[t].biome];
  }

  const hasTileData = Boolean(tiles && tiles.length > 0);

  // ── Render ─────────────────────────────────────────────────────────────

  return (
    <Svg
      width={size}
      height={size}
      viewBox={`${-vbHalf} ${-vbHalf} ${hs * 12} ${hs * 12}`}>

      {/* ── Tile polygons ─────────────────────────────────────────────── */}
      <G>
        {Array.from({ length: TILE_COUNT }, (_, t) => {
          const centre    = tileCentres[t];
          const disabled  = t === disabledTile;
          const pressable = onTilePress != null && !disabled;
          const tileNum   = hasTileData ? tiles![t]?.number : 0;
          const isRobber  = t === robberTile;

          return (
            <G
              key={t}
              opacity={disabled ? 0.3 : 1}
              onPress={pressable ? () => onTilePress!(t) : undefined}>

              {/* Hex body */}
              <Polygon
                points={polygonPoints(t)}
                fill={tileFill(t)}
                stroke={tileStroke(t)}
                strokeWidth={hs * 0.06}
              />

              {/* Tappable hit-target overlay (slightly transparent) */}
              {pressable && (
                <Polygon
                  points={polygonPoints(t)}
                  fill="transparent"
                  stroke="transparent"
                />
              )}

              {/* Number token — shows resource label above, number below */}
              {hasTileData && tileNum != null && tileNum > 0 && (
                <G>
                  <Circle
                    cx={centre.x}
                    cy={centre.y}
                    r={hs * 0.44}
                    fill="#f5f0e8"
                    stroke="#c8b878"
                    strokeWidth={hs * 0.05}
                  />
                  {/* Resource label (small, above number) */}
                  <SvgText
                    x={centre.x}
                    y={centre.y - hs * 0.08}
                    textAnchor="middle"
                    fontSize={hs * 0.18}
                    fontWeight="600"
                    fill="#666044">
                    {BIOME_LABEL[tiles![t].biome]}
                  </SvgText>
                  {/* Number (larger, below label) */}
                  <SvgText
                    x={centre.x}
                    y={centre.y + hs * 0.28}
                    textAnchor="middle"
                    fontSize={hs * 0.30}
                    fontWeight="bold"
                    fill={tileNum === 6 || tileNum === 8 ? '#b01010' : '#1a1a1a'}>
                    {tileNum}
                  </SvgText>
                </G>
              )}

              {/* Desert label (no number token) */}
              {hasTileData && (tileNum == null || tileNum === 0) && tiles![t]?.biome === Biome.DESERT && (
                <SvgText
                  x={centre.x}
                  y={centre.y + hs * 0.08}
                  textAnchor="middle"
                  fontSize={hs * 0.22}
                  fontWeight="600"
                  fill={BIOME_LABEL_COLOR[Biome.DESERT]}>
                  Desert
                </SvgText>
              )}

              {/* Robber marker */}
              {isRobber && (
                <Circle
                  cx={centre.x}
                  cy={centre.y}
                  r={hs * 0.28}
                  fill="#0a0a0a"
                  opacity={0.75}
                />
              )}

              {/* Debug tile index */}
              {showVertexIndices && (
                <SvgText
                  x={centre.x}
                  y={centre.y - hs * 0.15}
                  textAnchor="middle"
                  fontSize={hs * 0.2}
                  fill="#00e000">
                  {`T${t}`}
                </SvgText>
              )}
            </G>
          );
        })}
      </G>

      {/* ── Ports ──────────────────────────────────────────────────────── */}
      {showPorts && (
        <G>
          {PORTS.map((port, pi) => {
            const { anchor, a, b } = portAnchor(port, rotVerts, hs);
            const fill = PORT_FILL[port.type];
            return (
              <G key={pi}>
                <Line
                  x1={a.x} y1={a.y} x2={anchor.x} y2={anchor.y}
                  stroke={fill} strokeWidth={hs * 0.14} strokeLinecap="round" />
                <Line
                  x1={b.x} y1={b.y} x2={anchor.x} y2={anchor.y}
                  stroke={fill} strokeWidth={hs * 0.14} strokeLinecap="round" />
                <Circle
                  cx={anchor.x} cy={anchor.y}
                  r={hs * 0.32} fill={fill} />
                <SvgText
                  x={anchor.x}
                  y={anchor.y + hs * 0.11}
                  textAnchor="middle"
                  fontSize={hs * 0.26}
                  fontWeight="bold"
                  fill="#ffffff">
                  {PORT_LABELS[port.type]}
                </SvgText>
              </G>
            );
          })}
        </G>
      )}

      {/* ── Road outlines (white border for contrast against tiles) ────── */}
      {edges && edges.length > 0 && (
        <G>
          {edges.map((e, ei) => {
            if (!e) return null;
            const [v0, v1] = EDGE_VERTICES[ei];
            const p0 = rotVerts[v0];
            const p1 = rotVerts[v1];
            return (
              <Line
                key={`ro${ei}`}
                x1={p0.x} y1={p0.y}
                x2={p1.x} y2={p1.y}
                stroke="rgba(255,255,255,0.9)"
                strokeWidth={hs * 0.34}
                strokeLinecap="round"
              />
            );
          })}
        </G>
      )}

      {/* ── Roads ──────────────────────────────────────────────────────── */}
      {edges && edges.length > 0 && (
        <G>
          {edges.map((e, ei) => {
            if (!e) return null;
            const [v0, v1] = EDGE_VERTICES[ei];
            const p0 = rotVerts[v0];
            const p1 = rotVerts[v1];
            return (
              <Line
                key={ei}
                x1={p0.x} y1={p0.y}
                x2={p1.x} y2={p1.y}
                stroke={PLAYER_FILL[e.owner]}
                strokeWidth={hs * 0.20}
                strokeLinecap="round"
              />
            );
          })}
        </G>
      )}

      {/* ── Settlements & Cities ───────────────────────────────────────── */}
      {vertices && vertices.length > 0 && (
        <G>
          {vertices.map((v, vi) => {
            if (!v) return null;
            const pos = rotVerts[vi];
            if (v.city) {
              // City: larger filled circle
              return (
                <Circle
                  key={vi}
                  cx={pos.x} cy={pos.y}
                  r={hs * 0.28}
                  fill={PLAYER_FILL[v.owner]}
                  stroke="rgba(255,255,255,0.9)"
                  strokeWidth={hs * 0.10}
                />
              );
            }
            // Settlement: diamond (rotated square)
            const sq = hs * 0.20;
            return (
              <Polygon
                key={vi}
                points={[
                  `${pos.x},${pos.y - sq}`,
                  `${pos.x + sq},${pos.y}`,
                  `${pos.x},${pos.y + sq}`,
                  `${pos.x - sq},${pos.y}`,
                ].join(' ')}
                fill={PLAYER_FILL[v.owner]}
                stroke="rgba(255,255,255,0.9)"
                strokeWidth={hs * 0.10}
              />
            );
          })}
        </G>
      )}

      {/* ── Debug: edge indices ────────────────────────────────────────── */}
      {showEdgeIndices && (
        <G>
          {EDGE_VERTICES.map(([v0, v1], ei) => {
            const p0 = rotVerts[v0];
            const p1 = rotVerts[v1];
            return (
              <SvgText
                key={ei}
                x={(p0.x + p1.x) / 2}
                y={(p0.y + p1.y) / 2 + hs * 0.08}
                textAnchor="middle"
                fontSize={hs * 0.17}
                fill="#ff8000">
                {ei}
              </SvgText>
            );
          })}
        </G>
      )}

      {/* ── Debug: vertex indices ──────────────────────────────────────── */}
      {showVertexIndices && (
        <G>
          {rotVerts.map((pos, vi) => (
            <SvgText
              key={vi}
              x={pos.x}
              y={pos.y + hs * 0.08}
              textAnchor="middle"
              fontSize={hs * 0.16}
              fill="#00ccff">
              {vi}
            </SvgText>
          ))}
        </G>
      )}
    </Svg>
  );
}
