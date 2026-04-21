import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
} from 'react';
import { BleManager, Device, State, Subscription } from 'react-native-ble-plx';

import { CATAN_SERVICE_UUID, COMMAND_UUID, GAME_STATE_UUID, SCAN_TIMEOUT_MS } from '@/constants/ble';
import { BoardToPlayer, decodeBoardToPlayer, encodeAction, PlayerAction } from '@/services/proto';

// ── Types ─────────────────────────────────────────────────────────────────

export interface ScannedDevice {
  id: string;
  name: string;
  rssi: number | null;
}

export type ConnectionState = 'idle' | 'connecting' | 'connected' | 'disconnecting';

interface BleContextValue {
  bleState: State;
  scanning: boolean;
  devices: ScannedDevice[];
  connectionState: ConnectionState;
  connectedName: string | null;
  gameState: BoardToPlayer | null;
  startScan: () => void;
  stopScan: () => void;
  connect: (deviceId: string) => Promise<void>;
  disconnect: () => Promise<void>;
  sendAction: (action: PlayerAction) => Promise<void>;
}

// ── Context ───────────────────────────────────────────────────────────────

const BleContext = createContext<BleContextValue | null>(null);

// ── Provider ──────────────────────────────────────────────────────────────

export function BleProvider({ children }: { children: React.ReactNode }) {
  const managerRef = useRef<BleManager | null>(null);
  const deviceRef = useRef<Device | null>(null);
  const notifSubRef = useRef<Subscription | null>(null);
  const disconnectSubRef = useRef<Subscription | null>(null);
  const scanTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const [bleState, setBleState] = useState<State>(State.Unknown);
  const [scanning, setScanning] = useState(false);
  const [devices, setDevices] = useState<ScannedDevice[]>([]);
  const [connectionState, setConnectionState] = useState<ConnectionState>('idle');
  const [connectedName, setConnectedName] = useState<string | null>(null);
  const [gameState, setGameState] = useState<BoardToPlayer | null>(null);

  // Create BleManager once on mount
  useEffect(() => {
    const mgr = new BleManager();
    managerRef.current = mgr;

    const stateSub = mgr.onStateChange(state => {
      setBleState(state);
    }, true /* emit current state immediately */);

    return () => {
      stateSub.remove();
      if (scanTimerRef.current) clearTimeout(scanTimerRef.current);
      mgr.stopDeviceScan();
      mgr.destroy();
    };
  }, []);

  // ── Scan ────────────────────────────────────────────────────────────────

  const stopScan = useCallback(() => {
    if (scanTimerRef.current) {
      clearTimeout(scanTimerRef.current);
      scanTimerRef.current = null;
    }
    managerRef.current?.stopDeviceScan();
    setScanning(false);
  }, []);

  const startScan = useCallback(() => {
    const mgr = managerRef.current;
    if (!mgr || bleState !== State.PoweredOn) return;

    setDevices([]);
    setScanning(true);

    mgr.startDeviceScan(
      [CATAN_SERVICE_UUID],
      { allowDuplicates: false },
      (error, device) => {
        if (error) {
          setScanning(false);
          return;
        }
        if (device?.name?.startsWith('Catan-')) {
          setDevices(prev => {
            if (prev.some(d => d.id === device.id)) return prev;
            return [...prev, { id: device.id, name: device.name!, rssi: device.rssi }];
          });
        }
      },
    );

    scanTimerRef.current = setTimeout(stopScan, SCAN_TIMEOUT_MS);
  }, [bleState, stopScan]);

  // ── Connect ─────────────────────────────────────────────────────────────

  const connect = useCallback(
    async (deviceId: string) => {
      const mgr = managerRef.current;
      if (!mgr) return;

      stopScan();
      setConnectionState('connecting');

      try {
        const device = await mgr.connectToDevice(deviceId);
        await device.discoverAllServicesAndCharacteristics();

        deviceRef.current = device;
        setConnectedName(device.name ?? deviceId);

        // Handle unexpected disconnect
        disconnectSubRef.current = device.onDisconnected(() => {
          notifSubRef.current?.remove();
          notifSubRef.current = null;
          disconnectSubRef.current = null;
          deviceRef.current = null;
          setConnectedName(null);
          setGameState(null);
          setConnectionState('idle');
        });

        // Subscribe to GameState notifications
        notifSubRef.current = device.monitorCharacteristicForService(
          CATAN_SERVICE_UUID,
          GAME_STATE_UUID,
          (err, characteristic) => {
            if (err || !characteristic?.value) return;
            try {
              setGameState(decodeBoardToPlayer(characteristic.value));
            } catch {
              // Ignore malformed frames
            }
          },
        );

        // Read current state immediately so the screen isn't blank
        try {
          const char = await device.readCharacteristicForService(
            CATAN_SERVICE_UUID,
            GAME_STATE_UUID,
          );
          if (char.value) setGameState(decodeBoardToPlayer(char.value));
        } catch {
          // Non-critical — notifications will populate state soon
        }

        setConnectionState('connected');
      } catch (err) {
        setConnectionState('idle');
        throw err;
      }
    },
    [stopScan],
  );

  // ── Disconnect ───────────────────────────────────────────────────────────

  const disconnect = useCallback(async () => {
    const device = deviceRef.current;
    setConnectionState('disconnecting');

    notifSubRef.current?.remove();
    notifSubRef.current = null;
    disconnectSubRef.current?.remove();
    disconnectSubRef.current = null;

    try {
      await device?.cancelConnection();
    } catch {
      // Ignore — may already be disconnected
    }

    deviceRef.current = null;
    setConnectedName(null);
    setGameState(null);
    setConnectionState('idle');
  }, []);

  // ── Send action ──────────────────────────────────────────────────────────

  const sendAction = useCallback(async (action: PlayerAction) => {
    const device = deviceRef.current;
    if (!device) return;
    const payload = encodeAction(action);
    await device.writeCharacteristicWithResponseForService(
      CATAN_SERVICE_UUID,
      COMMAND_UUID,
      payload,
    );
  }, []);

  return (
    <BleContext.Provider
      value={{
        bleState,
        scanning,
        devices,
        connectionState,
        connectedName,
        gameState,
        startScan,
        stopScan,
        connect,
        disconnect,
        sendAction,
      }}>
      {children}
    </BleContext.Provider>
  );
}

// ── Hook ──────────────────────────────────────────────────────────────────

export function useBle() {
  const ctx = useContext(BleContext);
  if (!ctx) throw new Error('useBle must be used within BleProvider');
  return ctx;
}
