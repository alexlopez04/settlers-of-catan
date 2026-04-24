/**
 * BoardOverview — full-screen map of the Catan board.
 *
 * Renders the 19 tiles (biome + number token), 9 ports, placed roads,
 * settlements and cities with player colours, and the robber. Pinch to
 * zoom and scroll in any direction for accessibility. All geometry is
 * derived from `board-topology.ts` (which mirrors firmware topology).
 */
import React, { useMemo } from 'react';
import {
  Dimensions,
  Modal,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import Svg, {
  Circle,
  G,
  Line,
  Polygon,
  Rect,
  Text as SvgText,
} from 'react-native-svg';

import { Biome, BoardState, NO_TILE } from '@/services/proto';
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { useSettings } from '@/context/settings-context';
import {
  computeVertexPositions,
  EDGE_COUNT,
  EDGE_VERTICES,
  hexCorners,
  PORTS,
  PortType,
  portAnchor,
  TILE_COUNT,
  TILE_HEX_COORDS,
  tileCenter,
  VERTEX_COUNT,
} from '@/constants/board-topology';
import { SFSymbolIcon } from './symbol';

// ── Colour palettes ─────────────────────────────────────────────────────────

// Player 0..3 — distinct, reasonably colour-blind-friendly.
const PLAYER_COLORS = ['#D94A4A', '#3A6ACF', '#F08A1E', '#7B3FA6'];
const PLAYER_STROKE = '#1C1206';

interface BiomeCfg { fill: string; stroke: string; emoji: string; label: string; }
const BIOME_CONFIG: Record<Biome, BiomeCfg> = {
  [Biome.DESERT]:   { fill: '#E8D5A3', stroke: '#B89668', emoji: '🏜', label: 'Desert'   },
  [Biome.FOREST]:   { fill: '#3D7A32', stroke: '#25561D', emoji: '🪵', label: 'Lumber'   },
  [Biome.PASTURE]:  { fill: '#7ED06B', stroke: '#52A040', emoji: '🐑', label: 'Wool'     },
  [Biome.FIELD]:    { fill: '#F0C040', stroke: '#B88A18', emoji: '🌾', label: 'Grain'    },
  [Biome.HILL]:     { fill: '#C0602A', stroke: '#7A3A10', emoji: '🧱', label: 'Brick'    },
  [Biome.MOUNTAIN]: { fill: '#8090A8', stroke: '#526278', emoji: '⛏',  label: 'Ore'      },
};

const PORT_LABEL: Record<PortType, string> = {
  [PortType.GENERIC_3_1]: '3:1',
  [PortType.LUMBER_2_1]:  '🪵',
  [PortType.WOOL_2_1]:    '🐑',
  [PortType.GRAIN_2_1]:   '🌾',
  [PortType.BRICK_2_1]:   '🧱',
  [PortType.ORE_2_1]:     '⛏',
};

const PORT_FILL: Record<PortType, string> = {
  [PortType.GENERIC_3_1]: '#FFFFFF',
  [PortType.LUMBER_2_1]:  '#3D7A32',
  [PortType.WOOL_2_1]:    '#7ED06B',
  [PortType.GRAIN_2_1]:   '#F0C040',
  [PortType.BRICK_2_1]:   '#C0602A',
  [PortType.ORE_2_1]:     '#8090A8',
};

const NUMBER_DOTS: Record<number, number> = {
  2: 1, 3: 2, 4: 3, 5: 4, 6: 5, 8: 5, 9: 4, 10: 3, 11: 2, 12: 1,
};

const isHotNumber = (n: number) => n === 6 || n === 8;

// ── Props ───────────────────────────────────────────────────────────────────

interface Props {
  visible: boolean;
  onClose: () => void;
  boardState: BoardState | null;
}

// ── Component ───────────────────────────────────────────────────────────────

export function BoardOverview({ visible, onClose, boardState }: Props) {
  const theme = useTheme();
  const { debug } = useSettings();
  const { width: screenW } = Dimensions.get('window');

  // Choose a hex size that fills the screen width comfortably. The board
  // spans 5 hexes edge-to-edge (≈ 5*sqrt(3) ≈ 8.66 tile radii), plus a
  // margin for ports/road ticks — budget ~10.5 radii of width.
  const hexSize = useMemo(() => {
    const targetBoardWidth = screenW - 24;
    return Math.min(44, targetBoardWidth / 10.5);
  }, [screenW]);

  // Pre-compute all geometry once per hex size change.
  const geom = useMemo(() => computeGeometry(hexSize), [hexSize]);

  // SVG viewBox padding so ports/labels aren't clipped.
  const pad = hexSize * 0.4;
  const vbMinX = geom.minX - pad;
  const vbMinY = geom.minY - pad;
  const vbW    = (geom.maxX - geom.minX) + 2 * pad;
  const vbH    = (geom.maxY - geom.minY) + 2 * pad;

  // Display dimensions (1:1 with viewBox aspect). Zoom handled by ScrollView.
  const displayW = screenW - 16;
  const displayH = displayW * (vbH / vbW);

  const tiles   = boardState?.tiles ?? [];
  const verts   = boardState?.vertices ?? [];
  const edges   = boardState?.edges ?? [];
  const robber  = boardState?.robberTile ?? NO_TILE;

  return (
    <Modal
      visible={visible}
      animationType="slide"
      presentationStyle="pageSheet"
      onRequestClose={onClose}>
      <View style={[s.modal, { backgroundColor: theme.background }]}>
        <SafeAreaView style={s.safeArea} edges={['top', 'left', 'right']}>
          <View style={[s.header, { borderBottomColor: theme.backgroundElement }]}>
            <Text style={[s.title, { color: theme.text }]}>Board Overview</Text>
            <Pressable onPress={onClose} hitSlop={12} style={s.closeBtn}>
              <SFSymbolIcon name="xmark.circle.fill" size={28} color={theme.textSecondary} fallback="✕" />
            </Pressable>
          </View>

          <ScrollView
            style={s.scroll}
            contentContainerStyle={s.scrollContent}
            showsVerticalScrollIndicator={false}>
            {/* Zoom/pan surface. iOS: pinch + pan. */}
            <ScrollView
              style={[s.zoomScroll, { width: displayW, height: displayH }]}
              contentContainerStyle={{ width: displayW, height: displayH }}
              horizontal
              directionalLockEnabled={false}
              bouncesZoom
              pinchGestureEnabled
              minimumZoomScale={1}
              maximumZoomScale={3.5}
              showsHorizontalScrollIndicator={false}
              showsVerticalScrollIndicator={false}>
              <Svg
                width={displayW}
                height={displayH}
                viewBox={`${vbMinX} ${vbMinY} ${vbW} ${vbH}`}>
                {/* Sea background */}
                <Rect
                  x={vbMinX}
                  y={vbMinY}
                  width={vbW}
                  height={vbH}
                  fill="#9FC6E8"
                />

                {/* Ports (behind tiles; their dock lines extend past) */}
                {PORTS.map((port, i) => {
                  const { anchor, a, b } = portAnchor(port, geom.vertexPositions, hexSize);
                  const r = hexSize * 0.42;
                  const fill = PORT_FILL[port.type];
                  return (
                    <G key={`port-${i}`}>
                      <Line x1={anchor.x} y1={anchor.y} x2={a.x} y2={a.y}
                            stroke="#6B4A2A" strokeWidth={hexSize * 0.06} />
                      <Line x1={anchor.x} y1={anchor.y} x2={b.x} y2={b.y}
                            stroke="#6B4A2A" strokeWidth={hexSize * 0.06} />
                      <Circle cx={anchor.x} cy={anchor.y} r={r}
                              fill={fill} stroke="#6B4A2A" strokeWidth={hexSize * 0.05} />
                      <SvgText
                        x={anchor.x}
                        y={anchor.y + r * 0.28}
                        fontSize={r * 0.72}
                        fontWeight="700"
                        textAnchor="middle"
                        fill="#1C1206">
                        {PORT_LABEL[port.type]}
                      </SvgText>
                    </G>
                  );
                })}

                {/* Tiles */}
                {geom.tileCenters.map((centre, idx) => {
                  const tile  = tiles[idx];
                  const biome = tile?.biome ?? Biome.DESERT;
                  const num   = tile?.number ?? 0;
                  const cfg   = BIOME_CONFIG[biome] ?? BIOME_CONFIG[Biome.DESERT];
                  const pts   = hexCorners(centre, hexSize)
                                  .map(p => `${p.x},${p.y}`).join(' ');
                  return (
                    <G key={`tile-${idx}`}>
                      <Polygon
                        points={pts}
                        fill={cfg.fill}
                        stroke={cfg.stroke}
                        strokeWidth={hexSize * 0.04}
                        strokeLinejoin="round"
                      />
                      <SvgText
                        x={centre.x}
                        y={centre.y - hexSize * 0.28}
                        fontSize={hexSize * 0.5}
                        textAnchor="middle"
                        opacity={0.55}>
                        {cfg.emoji}
                      </SvgText>
                      {num > 0 && (
                        <NumberToken
                          cx={centre.x}
                          cy={centre.y + hexSize * 0.22}
                          r={hexSize * 0.32}
                          number={num}
                        />
                      )}
                    </G>
                  );
                })}

                {/* Roads */}
                {Array.from({ length: EDGE_COUNT }, (_, e) => {
                  const owner = edges[e]?.owner;
                  if (owner === undefined || owner === null) return null;
                  const [va, vb] = EDGE_VERTICES[e];
                  const p1 = geom.vertexPositions[va];
                  const p2 = geom.vertexPositions[vb];
                  // Inset so roads don't overlap vertex markers.
                  const t = 0.18;
                  const ax = p1.x + (p2.x - p1.x) * t;
                  const ay = p1.y + (p2.y - p1.y) * t;
                  const bx = p2.x + (p1.x - p2.x) * t;
                  const by = p2.y + (p1.y - p2.y) * t;
                  return (
                    <G key={`edge-${e}`}>
                      <Line x1={ax} y1={ay} x2={bx} y2={by}
                            stroke={PLAYER_STROKE} strokeWidth={hexSize * 0.22} strokeLinecap="round" />
                      <Line x1={ax} y1={ay} x2={bx} y2={by}
                            stroke={PLAYER_COLORS[owner] ?? '#888'}
                            strokeWidth={hexSize * 0.14} strokeLinecap="round" />
                    </G>
                  );
                })}

                {/* Settlements / cities */}
                {Array.from({ length: VERTEX_COUNT }, (_, v) => {
                  const vs = verts[v];
                  if (!vs) return null;
                  const p = geom.vertexPositions[v];
                  const fill = PLAYER_COLORS[vs.owner] ?? '#888';
                  if (vs.city) {
                    const side = hexSize * 0.36;
                    return (
                      <Rect
                        key={`v-${v}`}
                        x={p.x - side / 2}
                        y={p.y - side / 2}
                        width={side}
                        height={side}
                        rx={side * 0.18}
                        fill={fill}
                        stroke={PLAYER_STROKE}
                        strokeWidth={hexSize * 0.05}
                      />
                    );
                  }
                  return (
                    <Circle
                      key={`v-${v}`}
                      cx={p.x}
                      cy={p.y}
                      r={hexSize * 0.18}
                      fill={fill}
                      stroke={PLAYER_STROKE}
                      strokeWidth={hexSize * 0.05}
                    />
                  );
                })}

                {/* Debug: vertex indices (toggled via Settings → Debug → Vertex Number Overlay) */}
                {debug.vertexOverlay && Array.from({ length: VERTEX_COUNT }, (_, v) => {
                  const p = geom.vertexPositions[v];
                  return (
                    <G key={`dbg-v-${v}`}>
                      <Circle cx={p.x} cy={p.y} r={hexSize * 0.22} fill="rgba(0,0,0,0.55)" />
                      <SvgText
                        x={p.x}
                        y={p.y + hexSize * 0.08}
                        fontSize={hexSize * 0.22}
                        fontWeight="700"
                        textAnchor="middle"
                        fill="#FFFFFF">
                        {v}
                      </SvgText>
                    </G>
                  );
                })}

                {/* Debug: edge indices (toggled via Settings → Debug → Edge Number Overlay) */}
                {debug.edgeOverlay && Array.from({ length: EDGE_COUNT }, (_, e) => {
                  const [va, vb] = EDGE_VERTICES[e];
                  const pa = geom.vertexPositions[va];
                  const pb = geom.vertexPositions[vb];
                  const mx = (pa.x + pb.x) / 2;
                  const my = (pa.y + pb.y) / 2;
                  const r = hexSize * 0.2;
                  return (
                    <G key={`dbg-e-${e}`}>
                      <Circle cx={mx} cy={my} r={r} fill="rgba(180,60,0,0.75)" />
                      <SvgText
                        x={mx}
                        y={my + r * 0.35}
                        fontSize={r * 0.9}
                        fontWeight="700"
                        textAnchor="middle"
                        fill="#FFFFFF">
                        {e}
                      </SvgText>
                    </G>
                  );
                })}

                {/* Robber */}
                {robber < TILE_COUNT && (
                  <G>
                    <Circle
                      cx={geom.tileCenters[robber].x}
                      cy={geom.tileCenters[robber].y - hexSize * 0.55}
                      r={hexSize * 0.25}
                      fill="#1C1206"
                      stroke="#F5ECD7"
                      strokeWidth={hexSize * 0.05}
                    />
                    <SvgText
                      x={geom.tileCenters[robber].x}
                      y={geom.tileCenters[robber].y - hexSize * 0.46}
                      fontSize={hexSize * 0.32}
                      fontWeight="800"
                      textAnchor="middle"
                      fill="#F5ECD7">
                      R
                    </SvgText>
                  </G>
                )}
              </Svg>
            </ScrollView>

            {/* Legend — resources */}
            <View style={[s.legend, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.legendTitle, { color: theme.textSecondary }]}>RESOURCES</Text>
              <View style={s.legendGrid}>
                {([Biome.FOREST, Biome.PASTURE, Biome.FIELD, Biome.HILL, Biome.MOUNTAIN] as Biome[]).map(b => {
                  const cfg = BIOME_CONFIG[b];
                  return (
                    <View key={b} style={[s.legendChip, { backgroundColor: cfg.fill, borderColor: cfg.stroke }]}>
                      <Text style={s.legendEmoji}>{cfg.emoji}</Text>
                      <Text style={s.legendChipText}>{cfg.label}</Text>
                    </View>
                  );
                })}
              </View>
            </View>

            {/* Legend — players */}
            <View style={[s.legend, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.legendTitle, { color: theme.textSecondary }]}>PLAYERS</Text>
              <View style={s.legendGrid}>
                {PLAYER_COLORS.map((c, i) => (
                  <View key={i} style={[s.playerChip, { backgroundColor: c }]}>
                    <Text style={s.playerChipText}>P{i + 1}</Text>
                  </View>
                ))}
              </View>
              <Text style={[s.legendHint, { color: theme.textSecondary }]}>
                ● settlement  ■ city  ━ road
              </Text>
            </View>

            <Text style={[s.zoomHint, { color: theme.textSecondary }]}>
              Pinch to zoom · drag to pan
            </Text>

            <View style={{ height: Spacing.six }} />
          </ScrollView>
        </SafeAreaView>
      </View>
    </Modal>
  );
}

// ── Number token with pips ──────────────────────────────────────────────────

function NumberToken({ cx, cy, r, number }: { cx: number; cy: number; r: number; number: number }) {
  const hot   = isHotNumber(number);
  const color = hot ? '#C0392B' : '#1C1206';
  const dots  = NUMBER_DOTS[number] ?? 0;
  const gap   = r * 0.22;
  const startX = cx - ((dots - 1) * gap) / 2;
  const dotY  = cy + r * 0.5;
  return (
    <G>
      <Circle cx={cx} cy={cy} r={r} fill="#F5ECD7" stroke="#6B5A3E" strokeWidth={r * 0.08} />
      <SvgText
        x={cx}
        y={cy + r * 0.2}
        fontSize={r * 1.05}
        fontWeight="900"
        textAnchor="middle"
        fill={color}>
        {number}
      </SvgText>
      {Array.from({ length: dots }).map((_, i) => (
        <Circle
          key={i}
          cx={startX + i * gap}
          cy={dotY}
          r={r * 0.08}
          fill={color}
        />
      ))}
    </G>
  );
}

// ── Geometry pre-compute ────────────────────────────────────────────────────

interface Geom {
  tileCenters: { x: number; y: number }[];
  vertexPositions: { x: number; y: number }[];
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
}

function computeGeometry(hexSize: number): Geom {
  const tileCenters = TILE_HEX_COORDS.map(({ q, r }) => tileCenter(q, r, hexSize));
  const vertexPositions = computeVertexPositions(hexSize);

  // Start from vertex bounds (vertices are the outer edge of the island).
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const p of vertexPositions) {
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  // Expand to include port circles.
  for (const port of PORTS) {
    const { anchor } = portAnchor(port, vertexPositions, hexSize);
    const r = hexSize * 0.5;
    if (anchor.x - r < minX) minX = anchor.x - r;
    if (anchor.x + r > maxX) maxX = anchor.x + r;
    if (anchor.y - r < minY) minY = anchor.y - r;
    if (anchor.y + r > maxY) maxY = anchor.y + r;
  }
  return { tileCenters, vertexPositions, minX, minY, maxX, maxY };
}

// ── Styles ──────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  modal:    { flex: 1 },
  safeArea: { flex: 1 },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: Spacing.four,
    paddingVertical: Spacing.three,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  title:    { fontSize: 20, fontWeight: '700' },
  closeBtn: { padding: 4 },
  scroll:   { flex: 1 },
  scrollContent: {
    alignItems: 'center',
    paddingTop: Spacing.three,
    paddingBottom: Spacing.four,
    gap: Spacing.three,
  },
  zoomScroll: {
    borderRadius: 16,
    overflow: 'hidden',
    backgroundColor: '#9FC6E8',
  },
  legend: {
    borderRadius: 16,
    padding: Spacing.three,
    width: '92%',
    gap: Spacing.two,
  },
  legendTitle: {
    fontSize: 11,
    fontWeight: '700',
    letterSpacing: 0.8,
    textTransform: 'uppercase',
  },
  legendGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: Spacing.two,
  },
  legendChip: {
    flexDirection: 'row',
    alignItems: 'center',
    borderRadius: 10,
    paddingHorizontal: 10,
    paddingVertical: 5,
    gap: 5,
    borderWidth: 1.5,
  },
  legendEmoji:    { fontSize: 14 },
  legendChipText: { fontSize: 13, fontWeight: '700', color: '#1C1206' },
  legendHint:     { fontSize: 12, marginTop: 4 },
  playerChip: {
    minWidth: 48,
    alignItems: 'center',
    borderRadius: 10,
    paddingHorizontal: 10,
    paddingVertical: 5,
  },
  playerChipText: {
    fontSize: 13,
    fontWeight: '800',
    color: '#FFFFFF',
  },
  zoomHint: {
    fontSize: 12,
    fontStyle: 'italic',
  },
});
