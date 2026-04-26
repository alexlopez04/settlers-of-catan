// =============================================================================
// lobby-difficulty-picker.tsx — Board-difficulty picker shown during the LOBBY
// phase so Player 1 can choose how the board is generated.
//
// Only the player whose playerId === 0 (Player 1 / the host) sees interactive
// controls.  Other players see the current selection as read-only.
//
// The selection is sent as an ACTION_SET_DIFFICULTY message to the firmware,
// which stores it and uses it when the board is randomized at BOARD_SETUP.
// =============================================================================

import React, { useCallback } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { useBle } from '@/context/ble-context';
import { useTheme } from '@/hooks/use-theme';
import {
  Difficulty,
  DIFFICULTY_DESCRIPTION,
  DIFFICULTY_LABEL,
  PlayerAction,
} from '@/services/proto';
import { Spacing } from '@/constants/theme';

// ── Component ─────────────────────────────────────────────────────────────────

interface Props {
  currentDifficulty: Difficulty;
  myId: number;
}

export function LobbyDifficultyPicker({ currentDifficulty, myId }: Props) {
  const theme    = useTheme();
  const { sendInput, playerId } = useBle();

  const isHost = myId === 0;

  const selectDifficulty = useCallback(
    async (d: Difficulty) => {
      if (!isHost || d === currentDifficulty) return;
      try {
        await sendInput({
          playerId: playerId ?? 0,
          action: PlayerAction.SET_DIFFICULTY,
          monopolyRes: d,
        });
      } catch {
        // Ignore send errors — firmware will keep previous value.
      }
    },
    [isHost, currentDifficulty, sendInput, playerId],
  );

  const difficulties: Difficulty[] = [
    Difficulty.EASY,
    Difficulty.NORMAL,
    Difficulty.HARD,
    Difficulty.EXPERT,
  ];

  return (
    <View style={[s.root, { backgroundColor: theme.backgroundElement }]}>
      <Text style={[s.heading, { color: theme.textSecondary }]}>DIFFICULTY</Text>

      {!isHost && (
        <Text style={[s.guestNote, { color: theme.textSecondary }]}>
          Only Player 1 can change the difficulty.
        </Text>
      )}

      <View style={s.options}>
        {difficulties.map(d => {
          const selected = d === currentDifficulty;
          const active   = isHost && !selected;
          return (
            <Pressable
              key={d}
              onPress={() => selectDifficulty(d)}
              disabled={!isHost}
              style={({ pressed }) => [
                s.option,
                {
                  backgroundColor: selected
                    ? theme.primary
                    : theme.background,
                  borderColor: selected
                    ? theme.primary
                    : theme.backgroundElement,
                  opacity: pressed && active ? 0.6 : 1,
                },
              ]}>
              <Text
                style={[
                  s.optionLabel,
                  { color: selected ? '#fff' : theme.text },
                ]}>
                {DIFFICULTY_LABEL[d]}
              </Text>
              {selected && (
                <Text style={[s.optionDesc, { color: 'rgba(255,255,255,0.8)' }]}>
                  {DIFFICULTY_DESCRIPTION[d]}
                </Text>
              )}
              {!selected && isHost && (
                <Text style={[s.optionDesc, { color: theme.textSecondary }]}>
                  {DIFFICULTY_DESCRIPTION[d]}
                </Text>
              )}
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

// ── Styles ─────────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  root: {
    borderRadius: 12,
    padding: Spacing.four,
    gap: Spacing.three,
  },
  heading: {
    fontSize: 11,
    fontWeight: '700',
    letterSpacing: 1.2,
    textTransform: 'uppercase',
  },
  guestNote: {
    fontSize: 13,
    fontStyle: 'italic',
  },
  options: {
    gap: Spacing.two,
  },
  option: {
    borderRadius: 8,
    borderWidth: 1.5,
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.two,
    gap: 2,
  },
  optionLabel: {
    fontSize: 15,
    fontWeight: '600',
  },
  optionDesc: {
    fontSize: 12,
    lineHeight: 16,
  },
});
