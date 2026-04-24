import React from 'react';
import { Pressable, StyleSheet } from 'react-native';

import { SFSymbolIcon } from '@/components/ui/symbol';

interface StepperProps {
  onPress: () => void;
  decrement?: boolean;
  bg: string;
  fg: string;
}

export function Stepper({ onPress, decrement, bg, fg }: StepperProps) {
  return (
    <Pressable
      onPress={onPress}
      hitSlop={10}
      style={({ pressed }) => [s.stepper, { backgroundColor: bg, opacity: pressed ? 0.6 : 1 }]}>
      <SFSymbolIcon
        name={decrement ? 'minus' : 'plus'}
        size={16}
        color={fg}
        weight="semibold"
        fallback={decrement ? '−' : '+'}
      />
    </Pressable>
  );
}

const s = StyleSheet.create({
  stepper: {
    width: 36,
    height: 36,
    borderRadius: 18,
    alignItems: 'center',
    justifyContent: 'center',
  },
});
