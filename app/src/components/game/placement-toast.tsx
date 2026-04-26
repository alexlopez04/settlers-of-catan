import React, { useEffect, useRef, useState } from 'react';
import { Modal, Pressable, StyleSheet, Text, View } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withTiming,
  runOnJS,
} from 'react-native-reanimated';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { SFSymbolIcon } from '@/components/ui/symbol';
import type { useTheme } from '@/hooks/use-theme';

const ERROR_RED = '#B71C1C';
const ERROR_RED_MID = '#C62828';

interface PlacementToastProps {
  message: string | null;
  onClose: () => void;
  theme: ReturnType<typeof useTheme>;
}

export function PlacementToast({ message, onClose, theme }: PlacementToastProps) {
  const [visible, setVisible] = useState(false);
  const [displayedMessage, setDisplayedMessage] = useState<string | null>(null);
  const opacity = useSharedValue(0);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const insets = useSafeAreaInsets();

  const dismiss = () => {
    if (timerRef.current) clearTimeout(timerRef.current);
    opacity.value = withTiming(0, { duration: 220 }, (finished) => {
      if (finished) runOnJS(setVisible)(false);
    });
    onClose();
  };

  useEffect(() => {
    if (message) {
      setDisplayedMessage(message);
      setVisible(true);
      opacity.value = 0;
      opacity.value = withTiming(1, { duration: 180 });
      if (timerRef.current) clearTimeout(timerRef.current);
      timerRef.current = setTimeout(() => dismiss(), 3000);
    } else {
      if (visible) dismiss();
    }
    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
    };
  }, [message]);

  const screenStyle = useAnimatedStyle(() => ({
    opacity: opacity.value,
  }));

  const contentStyle = useAnimatedStyle(() => ({
    transform: [{ scale: 0.9 + 0.1 * opacity.value }],
  }));

  return (
    <Modal
      visible={visible}
      transparent={false}
      animationType="none"
      statusBarTranslucent
      onRequestClose={dismiss}
    >
      <Animated.View style={[s.fullScreen, screenStyle]}>
        {/* Close button — top-right */}
        <Pressable
          onPress={dismiss}
          hitSlop={16}
          style={[s.closeBtn, { top: insets.top + 12, right: 20 }]}
          accessibilityLabel="Dismiss alert"
        >
          <SFSymbolIcon name="xmark.circle.fill" size={34} color="rgba(255,255,255,0.75)" fallback="✕" />
        </Pressable>

        {/* Centred content */}
        <Animated.View style={[s.content, contentStyle]}>
          <SFSymbolIcon name="exclamationmark.octagon.fill" size={80} color="#fff" fallback="🚫" />
          <Text style={s.title}>Invalid Action</Text>
          <Text style={s.body}>{displayedMessage}</Text>
        </Animated.View>
      </Animated.View>
    </Modal>
  );
}

const s = StyleSheet.create({
  fullScreen: {
    flex: 1,
    backgroundColor: ERROR_RED,
  },
  closeBtn: {
    position: 'absolute',
    zIndex: 10,
  },
  content: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    paddingHorizontal: 36,
    gap: 20,
  },
  title: {
    fontSize: 34,
    fontWeight: '900',
    color: '#fff',
    letterSpacing: 0.3,
    textAlign: 'center',
  },
  body: {
    fontSize: 22,
    fontWeight: '600',
    color: 'rgba(255,255,255,0.92)',
    textAlign: 'center',
    lineHeight: 32,
  },
});
