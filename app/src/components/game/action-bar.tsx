import React from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { SFSymbolIcon } from '@/components/ui/symbol';
import { Spacing } from '@/constants/theme';
import { PlayerAction } from '@/services/proto';
import type { ButtonSpec } from '@/constants/game';

export type { ButtonSpec };

interface ActionBarProps {
  buttons: ButtonSpec[];
  onAction: (action: PlayerAction) => void;
  pendingAction: PlayerAction | null;
  primaryColor: string;
  surfaceColor: string;
  textColor: string;
  mutedColor: string;
}

export function ActionBar({
  buttons,
  onAction,
  pendingAction,
  primaryColor,
  surfaceColor,
  textColor,
  mutedColor,
}: ActionBarProps) {
  if (buttons.length === 0) return null;

  return (
    <SafeAreaView edges={['bottom']} style={[s.bar, { borderTopColor: surfaceColor }]}>
      <View style={s.inner}>
        {buttons.map(spec => {
          const isPending = pendingAction === spec.action;
          const disabled  = !spec.enabled || pendingAction !== null;
          const bg        = spec.primary ? primaryColor : surfaceColor;
          const fg        = spec.primary ? '#fff' : disabled ? mutedColor : textColor;
          return (
            <Pressable
              key={spec.action}
              onPress={() => onAction(spec.action)}
              disabled={disabled}
              style={({ pressed }) => [
                s.btn,
                {
                  backgroundColor: bg,
                  opacity: disabled ? 0.35 : pressed ? 0.75 : 1,
                  transform: [{ scale: pressed ? 0.97 : 1 }],
                },
              ]}>
              <View style={s.btnInner}>
                {isPending ? (
                  <Text style={[s.btnText, { color: fg }]}>…</Text>
                ) : (
                  <>
                    {spec.sfSymbol && (
                      <SFSymbolIcon name={spec.sfSymbol as any} size={18} color={fg} weight="semibold" />
                    )}
                    <Text style={[s.btnText, { color: fg }]} numberOfLines={1}>
                      {spec.label}
                    </Text>
                  </>
                )}
              </View>
            </Pressable>
          );
        })}
      </View>
    </SafeAreaView>
  );
}

const s = StyleSheet.create({
  bar: {
    borderTopWidth: StyleSheet.hairlineWidth,
  },
  inner: {
    flexDirection: 'row',
    padding: Spacing.three,
    gap: Spacing.two,
  },
  btn: {
    flex: 1,
    borderRadius: 14,
    paddingVertical: Spacing.three,
    alignItems: 'center',
    justifyContent: 'center',
    minHeight: 54,
  },
  btnInner: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
  },
  btnText: {
    fontSize: 15,
    fontWeight: '700',
    textAlign: 'center',
  },
});
