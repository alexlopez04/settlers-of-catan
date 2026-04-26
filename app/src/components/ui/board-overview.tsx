// =============================================================================
// board-overview.tsx — Full-screen modal board map (read-only, during play).
//
// Reads boardRotation from SettingsContext so the map always renders from the
// player's saved perspective.  The map is rendered at 2× the visible area so
// there is room to pan after the user pinches in; iOS ScrollView handles
// pinch-zoom natively via minimumZoomScale / maximumZoomScale.
// =============================================================================

import React from 'react';
import {
  Modal,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  useWindowDimensions,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { Biome } from '@/services/proto';
import type { BoardState } from '@/services/proto';
import { useSettings } from '@/context/settings-context';
import { useTheme } from '@/hooks/use-theme';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { BoardMap, BIOME_FILL, PLAYER_FILL } from '@/components/ui/board-map';
import { Spacing } from '@/constants/theme';
import { RESOURCES } from '@/constants/game';

// ── Constants ─────────────────────────────────────────────────────────────────

const PLAYER_LABELS = ['Player 1', 'Player 2', 'Player 3', 'Player 4'];
const PLAYER_TEXT   = ['#ffffff', '#ffffff', '#ffffff', '#1a1a1a'];

const BIOME_ROWS: { biome: Biome; label: string }[] = [
  { biome: Biome.FOREST,   label: 'Forest (Lumber)' },
  { biome: Biome.PASTURE,  label: 'Pasture (Wool)'  },
  { biome: Biome.FIELD,    label: 'Field (Grain)'   },
  { biome: Biome.HILL,     label: 'Hill (Brick)'    },
  { biome: Biome.MOUNTAIN, label: 'Mountain (Ore)'  },
  { biome: Biome.DESERT,   label: 'Desert'          },
];

// ── Zooming map ────────────────────────────────────────────────────────────────
// The SVG is rendered at the natural visible size (mapSize × mapSize).
// minimumZoomScale=1 keeps the whole board always in view; the user can pinch
// in up to 3× for detail and pan around freely.

interface ZoomableMapProps {
  mapSize: number;
  children: React.ReactNode;
}

function ZoomableMap({ mapSize, children }: ZoomableMapProps) {
  return (
    <ScrollView
      style={{ width: mapSize, height: mapSize }}
      contentContainerStyle={{ width: mapSize, height: mapSize }}
      minimumZoomScale={1}
      maximumZoomScale={3}
      bouncesZoom
      showsVerticalScrollIndicator={false}
      showsHorizontalScrollIndicator={false}
      centerContent
      scrollsToTop={false}>
      {children}
    </ScrollView>
  );
}

// ── Board key / legend ─────────────────────────────────────────────────────────

interface BoardKeyProps {
  boardState: BoardState | null;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}

function BoardKey({ boardState, theme }: BoardKeyProps) {
  // Only show player rows for connected / participating players.
  const numPlayers = boardState?.numPlayers ?? 0;
  const connMask   = boardState?.connectedMask ?? 0;
  const vp         = boardState?.vp ?? [];

  return (
    <View style={[k.root, { backgroundColor: theme.backgroundElement }]}>

      {/* ── Terrain key ───────────────────────────────────────────── */}
      <Text style={[k.sectionLabel, { color: theme.textSecondary }]}>TERRAIN</Text>
      <View style={k.grid}>
        {BIOME_ROWS.map(({ biome, label }) => (
          <View key={biome} style={k.entry}>
            <View style={[k.swatch, { backgroundColor: BIOME_FILL[biome], borderRadius: 5 }]} />
            <Text style={[k.entryLabel, { color: theme.text }]}>{label}</Text>
          </View>
        ))}
      </View>

      {/* ── Resource emoji reminder ──────────────────────────────── */}
      <Text style={[k.sectionLabel, { color: theme.textSecondary, marginTop: Spacing.two }]}>
        RESOURCES
      </Text>
      <View style={k.resRow}>
        {RESOURCES.map(r => (
          <View key={r.key} style={[k.resBadge, { backgroundColor: theme.background }]}>
            <Text style={k.resEmoji}>{r.emoji}</Text>
            <Text style={[k.resLabel, { color: theme.textSecondary }]}>{r.label}</Text>
          </View>
        ))}
      </View>

      {/* ── Player colours ───────────────────────────────────────── */}
      {numPlayers > 0 && (
        <>
          <Text style={[k.sectionLabel, { color: theme.textSecondary, marginTop: Spacing.two }]}>
            PLAYERS
          </Text>
          <View style={k.playerRow}>
            {Array.from({ length: numPlayers }, (_, i) => {
              const connected = Boolean(connMask & (1 << i));
              return (
                <View
                  key={i}
                  style={[
                    k.playerChip,
                    { backgroundColor: PLAYER_FILL[i], opacity: connected ? 1 : 0.4 },
                  ]}>
                  <Text style={[k.playerLabel, { color: PLAYER_TEXT[i] }]}>
                    P{i + 1}
                  </Text>
                  {vp[i] != null && (
                    <Text style={[k.playerVp, { color: PLAYER_TEXT[i] }]}>
                      {vp[i]} VP
                    </Text>
                  )}
                </View>
              );
            })}
          </View>
        </>
      )}
    </View>
  );
}

// ── Props ─────────────────────────────────────────────────────────────────────

export interface BoardOverviewProps {
  visible: boolean;
  onClose: () => void;
  boardState: BoardState | null;
}

// ── Component ─────────────────────────────────────────────────────────────────

export function BoardOverview({ visible, onClose, boardState }: BoardOverviewProps) {
  const theme                    = useTheme();
  const { boardRotation, debug } = useSettings();
  const { width }                = useWindowDimensions();

  // Visible map area: square, fills modal width up to a comfortable max.
  const mapSize = Math.min(width - Spacing.four * 2, 400);

  return (
    <Modal
      visible={visible}
      animationType="slide"
      presentationStyle="pageSheet"
      onRequestClose={onClose}>

      <View style={[s.root, { backgroundColor: theme.background }]}>
        <SafeAreaView style={s.safe} edges={['top', 'left', 'right']}>

          {/* Header */}
          <View style={[s.header, { borderBottomColor: theme.backgroundElement }]}>
            <Text style={[s.title, { color: theme.text }]}>Board</Text>
            <Pressable onPress={onClose} hitSlop={12} style={s.closeBtn}>
              <SFSymbolIcon
                name="xmark.circle.fill"
                size={28}
                color={theme.textSecondary}
                fallback="✕"
              />
            </Pressable>
          </View>

          <ScrollView
            contentContainerStyle={[s.content, { paddingHorizontal: Spacing.four }]}
            showsVerticalScrollIndicator={false}
            scrollsToTop={false}>

            {/* Zoomable hex map */}
            <ZoomableMap mapSize={mapSize}>
              <BoardMap
                tiles={boardState?.tiles ?? null}
                vertices={boardState?.vertices}
                edges={boardState?.edges}
                robberTile={boardState?.robberTile}
                rotation={boardRotation}
                size={mapSize}
                showPorts
                showVertexIndices={debug.vertexOverlay}
                showEdgeIndices={debug.edgeOverlay}
              />
            </ZoomableMap>

            {/* Zoom hint */}
            <Text style={[s.hint, { color: theme.textSecondary }]}>
              Pinch to zoom · drag to pan
            </Text>

            {/* Board key */}
            <BoardKey boardState={boardState} theme={theme} />

          </ScrollView>
        </SafeAreaView>
      </View>
    </Modal>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  root:     { flex: 1 },
  safe:     { flex: 1 },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: Spacing.four,
    paddingVertical: Spacing.three,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  title:    { fontSize: 20, fontWeight: '700' },
  closeBtn: { padding: Spacing.one },
  content:  {
    alignItems: 'center',
    paddingTop: Spacing.four,
    paddingBottom: Spacing.six,
    gap: Spacing.three,
  },
  hint: { fontSize: 11, fontWeight: '500' },
});

const k = StyleSheet.create({
  root: {
    width: '100%',
    borderRadius: 16,
    padding: Spacing.three,
    gap: Spacing.one,
  },
  sectionLabel: {
    fontSize: 11,
    fontWeight: '700',
    textTransform: 'uppercase',
    letterSpacing: 0.8,
    marginBottom: 2,
  },
  grid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: Spacing.one,
  },
  entry: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.one,
    width: '48%',
    paddingVertical: 3,
  },
  swatch: {
    width: 14,
    height: 14,
    borderRadius: 3,
    flexShrink: 0,
  },
  entryLabel: { fontSize: 12, fontWeight: '500', flexShrink: 1 },

  resRow: {
    flexDirection: 'row',
    gap: Spacing.one,
    flexWrap: 'wrap',
  },
  resBadge: {
    alignItems: 'center',
    paddingHorizontal: Spacing.two,
    paddingVertical: Spacing.one,
    borderRadius: 8,
    gap: 2,
  },
  resEmoji: { fontSize: 18 },
  resLabel: { fontSize: 10, fontWeight: '600' },

  playerRow: {
    flexDirection: 'row',
    gap: Spacing.two,
    flexWrap: 'wrap',
  },
  playerChip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.one,
    paddingHorizontal: Spacing.two,
    paddingVertical: Spacing.one,
    borderRadius: 8,
  },
  playerLabel: { fontSize: 13, fontWeight: '800' },
  playerVp:    { fontSize: 12, fontWeight: '600' },
});
