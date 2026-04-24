import React, { useEffect, useState } from 'react';
import { StyleSheet, Text } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withTiming,
  withSpring,
  withSequence,
  runOnJS,
} from 'react-native-reanimated';

import { SFSymbolIcon } from '@/components/ui/symbol';
import type { useTheme } from '@/hooks/use-theme';

const ERROR_RED = '#C0392B';
const ERROR_RED_DARK = '#96281B';

interface PlacementToastProps {
  message: string | null;
  theme: ReturnType<typeof useTheme>;
}

/**
 * A bold banner that slides in from the top of the screen and auto-dismisses.
 * Shown when the board rejects a piece placement for the current player.
 */
export function PlacementToast({ message, theme }: PlacementToastProps) {
  // Keep the last non-null message around so the exit animation can finish
  // before the view unmounts.
  const [displayedMessage, setDisplayedMessage] = useState<string | null>(null);
  const scale  = useSharedValue(0.85);
  const opacity = useSharedValue(0);
  const shakeX  = useSharedValue(0);

  useEffect(() => {
    if (message) {
      setDisplayedMessage(message);
      scale.value   = withSpring(1, { damping: 20, stiffness: 280 });
      opacity.value = withTiming(1, { duration: 120 });
      // Subtle shake
      shakeX.value = withSequence(
        withTiming( 5, { duration: 60 }),
        withTiming(-5, { duration: 60 }),
        withTiming( 3, { duration: 55 }),
        withTiming( 0, { duration: 50 }),
      );
    } else {
      scale.value   = withSpring(0.85, { damping: 20, stiffness: 280 });
      opacity.value = withTiming(0, { duration: 200 }, (finished) => {
        if (finished) runOnJS(setDisplayedMessage)(null);
      });
    }
  }, [message]);

  const animStyle = useAnimatedStyle(() => ({
    opacity: opacity.value,
    transform: [
      { scale: scale.value },
      { translateX: shakeX.value },
    ],
  }));

  if (!displayedMessage) return null;

  return (
    <Animated.View pointerEvents="none" style={[s.overlay, animStyle]}>
      <Animated.View style={[s.card, { backgroundColor: ERROR_RED, borderColor: ERROR_RED_DARK }]}>
        <SFSymbolIcon name="exclamationmark.octagon.fill" size={28} color="#fff" fallback="🚫" />
        <Text style={s.text} numberOfLines={3}>{displayedMessage}</Text>
      </Animated.View>
    </Animated.View>
  );
}

const s = StyleSheet.create({
  overlay: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    bottom: 0,
    justifyContent: 'center',
    alignItems: 'center',
    zIndex: 100,
  },
  card: {
    width: '82%',
    flexDirection: 'row',
    alignItems: 'center',
    gap: 14,
    borderRadius: 20,
    borderWidth: 2,
    paddingHorizontal: 22,
    paddingVertical: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.45,
    shadowRadius: 16,
    elevation: 14,
  },
  text: {
    flex: 1,
    fontSize: 17,
    fontWeight: '800',
    color: '#fff',
    letterSpacing: 0.2,
    lineHeight: 24,
  },
});
