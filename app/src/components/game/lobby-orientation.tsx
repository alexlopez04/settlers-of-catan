// =============================================================================
// lobby-orientation.tsx — Board-orientation picker shown during the LOBBY
// phase so each player can calibrate the map to their seat at the table.
//
// The physical board displays an asymmetric LED pattern (white strip at the
// "front" edge + a golden accent tile).  The player rotates this preview
// until the highlighted tiles match what they see on the physical board.
// The chosen rotation is persisted in SettingsContext.
// =============================================================================

import React from 'react';
import {
  Pressable,
  StyleSheet,
  Text,
  useWindowDimensions,
  View,
} from 'react-native';

import { useSettings } from '@/context/settings-context';
import { BoardRotation } from '@/utils/board-rotation';
import { BoardMap } from '@/components/ui/board-map';
import { useTheme } from '@/hooks/use-theme';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { Spacing } from '@/constants/theme';
import type { Tile } from '@/services/proto';

// ── Component ─────────────────────────────────────────────────────────────────

interface LobbyOrientationPickerProps {
  /** When provided, renders the real tile biome colours instead of the
   *  orientation-pattern highlight. Use this when tiles are already known
   *  (i.e. after the lobby). */
  tiles?: Tile[] | null;
}

export function LobbyOrientationPicker({ tiles }: LobbyOrientationPickerProps = {}) {
  const theme                      = useTheme();
  const { boardRotation, setBoardRotation } = useSettings();
  const { width }                  = useWindowDimensions();

  const hasTiles = (tiles?.length ?? 0) > 0;

  // Map fills the available width, capped at a comfortable size.
  const mapSize = Math.min(width - Spacing.four * 2 - 80, 280);

  const rotate = (delta: 1 | -1) => {
    const next = (((boardRotation + delta) % 6) + 6) % 6;
    setBoardRotation(next as BoardRotation);
  };

  return (
    <View style={[s.root, { backgroundColor: theme.backgroundElement }]}>

      <Text style={[s.heading, { color: theme.textSecondary }]}>
        YOUR PERSPECTIVE
      </Text>

      <Text style={[s.hint, { color: theme.textSecondary }]}>
        {hasTiles
          ? 'Rotate until the board matches your view of the physical board.'
          : 'Rotate until the glowing tiles match your view of the physical board.'}
      </Text>

      {/* Map + rotate buttons in a row */}
      <View style={s.mapRow}>

        <Pressable
          onPress={() => rotate(-1)}
          hitSlop={16}
          style={({ pressed }) => [
            s.rotateBtn,
            { backgroundColor: theme.background, opacity: pressed ? 0.5 : 1 },
          ]}>
          <SFSymbolIcon
            name="arrow.counterclockwise"
            size={22}
            color={theme.text}
            fallback="⟲"
          />
        </Pressable>

        <BoardMap
          tiles={hasTiles ? tiles : null}
          rotation={boardRotation}
          size={mapSize}
          showOrientationPattern={!hasTiles}
          showPorts={false}
        />

        <Pressable
          onPress={() => rotate(1)}
          hitSlop={16}
          style={({ pressed }) => [
            s.rotateBtn,
            { backgroundColor: theme.background, opacity: pressed ? 0.5 : 1 },
          ]}>
          <SFSymbolIcon
            name="arrow.clockwise"
            size={22}
            color={theme.text}
            fallback="⟳"
          />
        </Pressable>

      </View>

      {/* Six-dot step indicator */}
      <View style={s.dots}>
        {([0, 1, 2, 3, 4, 5] as BoardRotation[]).map(i => (
          <Pressable
            key={i}
            onPress={() => setBoardRotation(i)}
            hitSlop={8}
            style={[
              s.dot,
              {
                backgroundColor:
                  i === boardRotation ? theme.primary : theme.background,
                borderColor: theme.primary,
              },
            ]}
          />
        ))}
      </View>

      <Text style={[s.rotLabel, { color: theme.textSecondary }]}>
        {boardRotation * 60}° rotation
      </Text>

    </View>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  root: {
    borderRadius: 16,
    padding: Spacing.three,
    alignItems: 'center',
    gap: Spacing.two,
  },
  heading: {
    fontSize: 11,
    fontWeight: '700',
    textTransform: 'uppercase',
    letterSpacing: 0.8,
  },
  hint: {
    fontSize: 12,
    fontWeight: '500',
    textAlign: 'center',
    paddingHorizontal: Spacing.three,
  },
  mapRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.three,
    marginVertical: Spacing.two,
  },
  rotateBtn: {
    width: 44,
    height: 44,
    borderRadius: 22,
    alignItems: 'center',
    justifyContent: 'center',
  },
  dots: {
    flexDirection: 'row',
    gap: Spacing.two,
    marginTop: Spacing.one,
  },
  dot: {
    width: 10,
    height: 10,
    borderRadius: 5,
    borderWidth: 1.5,
  },
  rotLabel: {
    fontSize: 11,
    fontWeight: '600',
  },
});
