import React, { useEffect } from 'react';
import { StyleSheet, Text } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withTiming,
  withRepeat,
  withSequence,
} from 'react-native-reanimated';

import { SFSymbolIcon } from '@/components/ui/symbol';
import type { useTheme } from '@/hooks/use-theme';

interface SyncIndicatorProps {
  syncing: boolean;
  lastSynced: number | null;
  theme: ReturnType<typeof useTheme>;
}

export function SyncIndicator({ syncing, lastSynced, theme }: SyncIndicatorProps) {
  const opacity = useSharedValue(0);

  useEffect(() => {
    if (syncing) {
      opacity.value = withRepeat(
        withSequence(
          withTiming(1, { duration: 400 }),
          withTiming(0.3, { duration: 400 }),
        ),
        -1,
        false,
      );
    } else {
      opacity.value = withTiming(0.7, { duration: 200 });
    }
  }, [syncing]);

  const animStyle = useAnimatedStyle(() => ({ opacity: opacity.value }));

  if (!syncing && !lastSynced) return null;

  return (
    <Animated.View style={[s.row, animStyle]}>
      <SFSymbolIcon name="arrow.triangle.2.circlepath" size={13} color={theme.textSecondary} fallback="↻" />
      <Text style={[s.text, { color: theme.textSecondary }]}>
        {syncing ? 'Syncing…' : 'Synced'}
      </Text>
    </Animated.View>
  );
}

const s = StyleSheet.create({
  row:  { flexDirection: 'row', alignItems: 'center', gap: 4 },
  text: { fontSize: 12 },
});
