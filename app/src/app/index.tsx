import { router } from 'expo-router';
import React, { useCallback, useEffect, useState } from 'react';
import {
  ActivityIndicator,
  FlatList,
  Pressable,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { State } from 'react-native-ble-plx';

import { ScannedDevice, useBle } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';

export default function ScanScreen() {
  const theme = useTheme();
  const { bleState, scanning, devices, connectionState, startScan, stopScan, connect } = useBle();
  const [connectingTo, setConnectingTo] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  // Auto-scan when Bluetooth powers on
  useEffect(() => {
    if (bleState === State.PoweredOn) {
      startScan();
    }
  }, [bleState]);

  // Navigate to game when connected
  useEffect(() => {
    if (connectionState === 'connected') {
      router.push('/game');
      setConnectingTo(null);
    }
  }, [connectionState]);

  const handleConnect = useCallback(
    async (device: ScannedDevice) => {
      setError(null);
      setConnectingTo(device.id);
      try {
        await connect(device.id);
      } catch {
        setError(`Could not connect to ${device.name}. Please try again.`);
        setConnectingTo(null);
      }
    },
    [connect],
  );

  const handleScanToggle = useCallback(() => {
    if (scanning) {
      stopScan();
    } else {
      setError(null);
      startScan();
    }
  }, [scanning, startScan, stopScan]);

  const renderDevice = useCallback(
    ({ item }: { item: ScannedDevice }) => {
      const isConnecting = connectingTo === item.id;
      const rssiStrength =
        item.rssi == null ? '' : item.rssi > -60 ? '●●●' : item.rssi > -80 ? '●●○' : '●○○';

      return (
        <Pressable
          onPress={() => handleConnect(item)}
          disabled={connectingTo !== null}
          style={({ pressed }) => [
            styles.deviceRow,
            {
              backgroundColor: theme.backgroundElement,
              borderColor: theme.backgroundSelected,
              opacity: pressed || (connectingTo !== null && !isConnecting) ? 0.5 : 1,
            },
          ]}>
          <View style={styles.deviceInfo}>
            <Text style={[styles.deviceName, { color: theme.text }]}>{item.name}</Text>
            <Text style={[styles.deviceId, { color: theme.textSecondary }]}>Catan board</Text>
          </View>
          <View style={styles.deviceRight}>
            {item.rssi != null && (
              <Text style={[styles.rssi, { color: theme.textSecondary }]}>{rssiStrength}</Text>
            )}
            {isConnecting ? (
              <ActivityIndicator size="small" color={theme.primary} />
            ) : (
              <Text style={[styles.connectChevron, { color: theme.primary }]}>›</Text>
            )}
          </View>
        </Pressable>
      );
    },
    [connectingTo, handleConnect, theme],
  );

  const bleBanner = () => {
    if (bleState === State.PoweredOff) {
      return (
        <View style={[styles.banner, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[styles.bannerText, { color: theme.text }]}>
            🔵  Bluetooth is off — enable it to find stations.
          </Text>
        </View>
      );
    }
    if (bleState === State.Unauthorized) {
      return (
        <View style={[styles.banner, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[styles.bannerText, { color: theme.text }]}>
            🔒  Bluetooth permission denied. Allow it in Settings.
          </Text>
        </View>
      );
    }
    if (bleState === State.Unsupported) {
      return (
        <View style={[styles.banner, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[styles.bannerText, { color: theme.text }]}>
            ⚠️  Bluetooth is not supported on this device.
          </Text>
        </View>
      );
    }
    return null;
  };

  return (
    <View style={[styles.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={styles.safeArea} edges={['top', 'left', 'right']}>
        {/* Header */}
        <View style={styles.header}>
          <Text style={[styles.title, { color: theme.text }]}>Settlers of Catan</Text>
          <Text style={[styles.subtitle, { color: theme.textSecondary }]}>
            Mobile Player Console
          </Text>
        </View>

        {bleBanner()}

        {/* Scan status */}
        {bleState === State.PoweredOn && (
          <View style={styles.scanStatus}>
            {scanning ? (
              <View style={styles.scanningRow}>
                <ActivityIndicator size="small" color={theme.primary} />
                <Text style={[styles.scanningText, { color: theme.textSecondary }]}>
                  Looking for the Catan board…
                </Text>
              </View>
            ) : (
              <Text style={[styles.scanningText, { color: theme.textSecondary }]}>
                {devices.length === 0 ? 'No board found.' : 'Board ready'}
              </Text>
            )}
          </View>
        )}

        {/* Device list */}
        <FlatList
          data={devices}
          keyExtractor={d => d.id}
          renderItem={renderDevice}
          contentContainerStyle={styles.list}
          ItemSeparatorComponent={() => <View style={styles.separator} />}
          ListEmptyComponent={
            bleState === State.PoweredOn && !scanning ? (
              <Text style={[styles.emptyText, { color: theme.textSecondary }]}>
                Make sure the Catan board is powered on and nearby.
              </Text>
            ) : null
          }
        />

        {/* Error */}
        {error && (
          <Text style={[styles.errorText, { color: theme.danger }]}>{error}</Text>
        )}

        {/* Scan button */}
        {bleState === State.PoweredOn && (
          <View style={[styles.footer, { borderTopColor: theme.backgroundElement }]}>
            <Pressable
              onPress={handleScanToggle}
              disabled={connectingTo !== null}
              style={({ pressed }) => [
                styles.scanButton,
                { backgroundColor: theme.primary, opacity: pressed ? 0.8 : 1 },
              ]}>
              <Text style={styles.scanButtonText}>
                {scanning ? 'Stop Scanning' : 'Scan Again'}
              </Text>
            </Pressable>
          </View>
        )}
      </SafeAreaView>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
  },
  safeArea: {
    flex: 1,
  },
  header: {
    alignItems: 'center',
    paddingTop: Spacing.six,
    paddingBottom: Spacing.four,
    paddingHorizontal: Spacing.four,
  },
  title: {
    fontSize: 32,
    fontWeight: '700',
    letterSpacing: 0.5,
    textAlign: 'center',
  },
  subtitle: {
    fontSize: 15,
    marginTop: Spacing.one,
    textAlign: 'center',
  },
  banner: {
    marginHorizontal: Spacing.four,
    marginBottom: Spacing.three,
    padding: Spacing.three,
    borderRadius: 12,
  },
  bannerText: {
    fontSize: 15,
    textAlign: 'center',
  },
  scanStatus: {
    paddingHorizontal: Spacing.four,
    paddingBottom: Spacing.two,
    minHeight: 28,
  },
  scanningRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.two,
  },
  scanningText: {
    fontSize: 14,
  },
  list: {
    paddingHorizontal: Spacing.four,
    paddingBottom: Spacing.three,
  },
  separator: {
    height: Spacing.two,
  },
  deviceRow: {
    flexDirection: 'row',
    alignItems: 'center',
    padding: Spacing.three,
    borderRadius: 14,
    borderWidth: 1,
  },
  deviceInfo: {
    flex: 1,
  },
  deviceName: {
    fontSize: 17,
    fontWeight: '600',
  },
  deviceId: {
    fontSize: 13,
    marginTop: 2,
  },
  deviceRight: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.two,
  },
  rssi: {
    fontSize: 13,
    fontVariant: ['tabular-nums'],
  },
  connectChevron: {
    fontSize: 26,
    fontWeight: '300',
    lineHeight: 28,
  },
  emptyText: {
    textAlign: 'center',
    fontSize: 14,
    paddingTop: Spacing.four,
    paddingHorizontal: Spacing.four,
  },
  errorText: {
    textAlign: 'center',
    fontSize: 14,
    paddingHorizontal: Spacing.four,
    paddingBottom: Spacing.two,
  },
  footer: {
    padding: Spacing.four,
    borderTopWidth: StyleSheet.hairlineWidth,
  },
  scanButton: {
    borderRadius: 14,
    paddingVertical: Spacing.three,
    alignItems: 'center',
  },
  scanButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
});
