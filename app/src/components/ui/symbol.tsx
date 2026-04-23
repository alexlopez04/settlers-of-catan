/**
 * Thin wrapper around expo-symbols SymbolView.
 * Provides a consistent cross-platform fallback using a Text element
 * for any platform where SF Symbols are unavailable.
 */
import { SymbolView } from 'expo-symbols';
import type { SFSymbol } from 'expo-symbols';
import React from 'react';
import { Platform, Text } from 'react-native';

interface Props {
  name: SFSymbol;
  size?: number;
  color?: string;
  weight?: 'ultraLight' | 'thin' | 'light' | 'regular' | 'medium' | 'semibold' | 'bold' | 'heavy' | 'black';
  type?: 'monochrome' | 'hierarchical' | 'palette' | 'multicolor';
  fallback?: string;
}

export function SFSymbolIcon({ name, size = 22, color = '#000', weight = 'regular', type = 'monochrome', fallback }: Props) {
  if (Platform.OS === 'ios') {
    return (
      <SymbolView
        name={name}
        size={size}
        tintColor={color}
        weight={weight}
        type={type}
      />
    );
  }
  // Android / web fallback
  if (fallback) {
    return <Text style={{ fontSize: size * 0.8, color }}>{fallback}</Text>;
  }
  return null;
}
