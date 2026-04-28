import '@/global.css';

import { Platform } from 'react-native';

export const Colors = {
  light: {
    text: '#1C1206',
    textSecondary: '#6B5A3E',
    background: '#F5ECD7',
    backgroundElement: '#EAD9BC',
    backgroundSelected: '#DCC9A5',
    primary: '#8B4513',
    danger: '#C0392B',
  },
  dark: {
    text: '#F0E8D0',
    textSecondary: '#A8A090',
    background: '#0E1A0E',
    backgroundElement: '#1A2B1A',
    backgroundSelected: '#2A3D2A',
    primary: '#C8832A',
    danger: '#E05A3A',
  },
} as const;

export type ThemeColor = keyof typeof Colors.light & keyof typeof Colors.dark;

export const Fonts = Platform.select({
  ios: {
    /** iOS `UIFontDescriptorSystemDesignDefault` */
    sans: 'system-ui',
    /** iOS `UIFontDescriptorSystemDesignSerif` */
    serif: 'ui-serif',
    /** iOS `UIFontDescriptorSystemDesignRounded` */
    rounded: 'ui-rounded',
    /** iOS `UIFontDescriptorSystemDesignMonospaced` */
    mono: 'ui-monospace',
  },
  default: {
    sans: 'normal',
    serif: 'serif',
    rounded: 'normal',
    mono: 'monospace',
  },
  web: {
    sans: 'var(--font-display)',
    serif: 'var(--font-serif)',
    rounded: 'var(--font-rounded)',
    mono: 'var(--font-mono)',
  },
});

export const Spacing = {
  half: 2,
  one: 4,
  two: 8,
  three: 16,
  four: 24,
  five: 32,
  six: 64,
} as const;

export const BottomTabInset = Platform.select({ ios: 50, android: 80 }) ?? 0;
export const MaxContentWidth = 800;
