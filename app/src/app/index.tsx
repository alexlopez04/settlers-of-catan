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
import { useSettings } from '@/context/settings-context';
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { SFSymbolIcon } from '@/components/ui/symbol';

// ── Screen ────────────────────────────────────────────────────────────────────

export default function ScanScreen() {
  const theme = useTheme();
  const { bleState, scanning, devices, connectionState, startScan, stopScan, connect, connectSimulated } = useBle();
  const { debug } = useSettings();
  const [connectingTo, setConnectingTo] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  // Auto-scan when Bluetooth powers on.
  useEffect(() => {
    if (bleState === State.PoweredOn) startScan();
  }, [bleState, startScan]);

  // Navigate to game when connected.
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
        item.rssi == null ? null : item.rssi > -60 ? 3 : item.rssi > -80 ? 2 : 1;

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
          {/* Board icon */}
          <View style={[styles.deviceIcon, { backgroundColor: theme.backgroundSelected }]}>
            <SFSymbolIcon name="dot.radiowaves.left.and.right" size={20} color={theme.primary} fallback="⊡" />
          </View>

          <View style={styles.deviceInfo}>
            <Text style={[styles.deviceName, { color: theme.text }]}>{item.name}</Text>
            <Text style={[styles.deviceId, { color: theme.textSecondary }]}>Catan board</Text>
          </View>

          <View style={styles.deviceRight}>
            {rssiStrength != null && (
              <SFSymbolIcon
                name={
                  rssiStrength === 3
                    ? 'wifi'
                    : rssiStrength === 2
                    ? 'wifi.exclamationmark'
                    : 'wifi.slash'
                }
                size={16}
                color={theme.textSecondary}
                fallback={rssiStrength === 3 ? '●●●' : rssiStrength === 2 ? '●●○' : '●○○'}
              />
            )}
            {isConnecting ? (
              <ActivityIndicator size="small" color={theme.primary} />
            ) : (
              <SFSymbolIcon name="chevron.right" size={16} color={theme.primary} weight="semibold" fallback="›" />
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
          <SFSymbolIcon name="antenna.radiowaves.left.and.right.slash" size={18} color={theme.text} fallback="⊘" />
          <Text style={[styles.bannerText, { color: theme.text }]}>
            Bluetooth is off — enable it to find stations.
          </Text>
        </View>
      );
    }
    if (bleState === State.Unauthorized) {
      return (
        <View style={[styles.banner, { backgroundColor: theme.backgroundElement }]}>
          <SFSymbolIcon name="lock.shield" size={18} color={theme.text} fallback="🔒" />
          <Text style={[styles.bannerText, { color: theme.text }]}>
            Bluetooth permission denied. Allow it in Settings.
          </Text>
        </View>
      );
    }
    if (bleState === State.Unsupported) {
      return (
        <View style={[styles.banner, { backgroundColor: theme.backgroundElement }]}>
          <SFSymbolIcon name="exclamationmark.triangle" size={18} color={theme.text} fallback="⚠️" />
          <Text style={[styles.bannerText, { color: theme.text }]}>
            Bluetooth is not supported on this device.
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
          <Pressable
            onPress={() => router.push('/settings')}
            hitSlop={12}
            style={({ pressed }) => [styles.headerBtn, { opacity: pressed ? 0.5 : 1 }]}>
            <SFSymbolIcon name="gearshape" size={22} color={theme.textSecondary} fallback="⚙️" />
          </Pressable>
          <View style={styles.headerCenter}>
            <Text style={[styles.title, { color: theme.text }]}>Settlers of Catan</Text>
            <Text style={[styles.subtitle, { color: theme.textSecondary }]}>
              Mobile Player Client
            </Text>
          </View>
          <Pressable
            onPress={() => router.push('/rules')}
            hitSlop={12}
            style={({ pressed }) => [styles.headerBtn, { opacity: pressed ? 0.5 : 1 }]}>
            <SFSymbolIcon name="book.closed" size={22} color={theme.textSecondary} fallback="📖" />
          </Pressable>
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
                {devices.length === 0 ? 'No board found.' : 'Board found — tap to connect'}
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
                {
                  backgroundColor: scanning ? theme.backgroundElement : theme.primary,
                  opacity: pressed || connectingTo !== null ? 0.7 : 1,
                },
              ]}>
              <View style={styles.scanButtonInner}>
                <SFSymbolIcon
                  name={scanning ? 'stop.circle' : 'antenna.radiowaves.left.and.right'}
                  size={18}
                  color={scanning ? theme.text : '#fff'}
                  weight="semibold"
                  fallback={scanning ? '■' : '⊙'}
                />
                <Text style={[styles.scanButtonText, { color: scanning ? theme.text : '#fff' }]}>
                  {scanning ? 'Stop Scanning' : 'Scan Again'}
                </Text>
              </View>
            </Pressable>
          </View>
        )}

        {/* Debug: simulated board (only visible when the toggle is on in Settings → Debug) */}
        {debug.simulatedBoard && connectionState === 'idle' && (
          <View style={[styles.footer, { borderTopColor: theme.backgroundElement }]}>
            <Pressable
              onPress={connectSimulated}
              style={({ pressed }) => [
                styles.scanButton,
                { backgroundColor: theme.backgroundElement, opacity: pressed ? 0.7 : 1 },
              ]}>
              <View style={styles.scanButtonInner}>
                <SFSymbolIcon name="cpu" size={18} color={theme.text} fallback="⊡" />
                <Text style={[styles.scanButtonText, { color: theme.text }]}>
                  Connect to Simulated Board
                </Text>
              </View>
            </Pressable>
          </View>
        )}
      </SafeAreaView>
    </View>
  );
}



// ── Styles ────────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  root:     { flex: 1 },
  safeArea: { flex: 1 },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingTop: Spacing.four,
    paddingBottom: Spacing.three,
    paddingHorizontal: Spacing.three,
  },
  headerBtn: {
    width: 44,
    height: 44,
    alignItems: 'center',
    justifyContent: 'center',
  },
  headerCenter: {
    flex: 1,
    alignItems: 'center',
  },
  title: {
    fontSize: 26,
    fontWeight: '700',
    letterSpacing: 0.5,
    textAlign: 'center',
  },
  subtitle: {
    fontSize: 13,
    marginTop: 2,
    textAlign: 'center',
  },
  _banner_anchor: {},
  banner: {
    marginHorizontal: Spacing.four,
    marginBottom: Spacing.three,
    padding: Spacing.three,
    borderRadius: 12,
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.two,
  },
  bannerText: {
    fontSize: 15,
    flex: 1,
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
    gap: Spacing.two,
  },
  deviceIcon: {
    width: 40,
    height: 40,
    borderRadius: 12,
    alignItems: 'center',
    justifyContent: 'center',
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
  scanButtonInner: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.two,
  },
  scanButtonText: {
    fontSize: 16,
    fontWeight: '600',
  },
});
