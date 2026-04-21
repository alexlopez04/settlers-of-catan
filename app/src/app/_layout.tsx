import { DarkTheme, DefaultTheme, ThemeProvider } from '@react-navigation/native';
import { Stack } from 'expo-router';
import React from 'react';
import { useColorScheme } from 'react-native';

import { BleProvider } from '@/context/ble-context';

export default function RootLayout() {
  const colorScheme = useColorScheme();
  return (
    <ThemeProvider value={colorScheme === 'dark' ? DarkTheme : DefaultTheme}>
      <BleProvider>
        <Stack screenOptions={{ headerShown: false, animation: 'fade' }}>
          <Stack.Screen name="index" />
          <Stack.Screen name="game" />
        </Stack>
      </BleProvider>
    </ThemeProvider>
  );
}
