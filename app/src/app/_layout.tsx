import { DarkTheme, DefaultTheme, ThemeProvider } from '@react-navigation/native';
import { Stack } from 'expo-router';
import React from 'react';
import { useColorScheme } from 'react-native';
import { GestureHandlerRootView } from 'react-native-gesture-handler';

import { BleProvider } from '@/context/ble-context';
import { SettingsProvider } from '@/context/settings-context';
import { TutorialProvider } from '@/context/tutorial-context';

export default function RootLayout() {
  const colorScheme = useColorScheme();
  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      <ThemeProvider value={colorScheme === 'dark' ? DarkTheme : DefaultTheme}>
      <SettingsProvider>
        <TutorialProvider>
          <BleProvider>
            <Stack screenOptions={{ headerShown: false, animation: 'fade' }}>
              <Stack.Screen name="index" />
              <Stack.Screen name="game" options={{ gestureEnabled: false }} />
              <Stack.Screen name="settings" options={{ animation: 'slide_from_right' }} />
              <Stack.Screen name="rules" options={{ animation: 'slide_from_right' }} />
            </Stack>
          </BleProvider>
        </TutorialProvider>
      </SettingsProvider>
      </ThemeProvider>
    </GestureHandlerRootView>
  );
}
