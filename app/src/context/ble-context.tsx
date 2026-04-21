import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
} from 'react';
import { BleManager, Device, State, Subscription } from 'react-native-ble-plx';

import {
  CATAN_SERVICE_UUID,
  COMMAND_UUID,
  GAME_STATE_UUID,
  SCAN_TIMEOUT_MS,
} from '@/constants/ble';
import {
  BoardState,
  PlayerAction,
  PlayerInput,
  decodeBoardStateFrame,
  encodeAction,
  encodePlayerInput,
  encodeReport,
} from '@/services/proto';

// ── Types ─────────────────────────────────────────────────────────────────

export interface ScannedDevice {
  id: string;
  name: string;
  rssi: number | null;
}

export type ConnectionState = 'idle' | 'connecting' | 'connected' | 'disconnecting';

export interface ResourceCounts {
  lumber: number;
  wool: number;
  grain: number;
  brick: number;
  ore: number;
}

interface BleContextValue {
  bleState: State;
  scanning: boolean;
  devices: ScannedDevice[];
  connectionState: ConnectionState;
  connectedName: string | null;
  /** Player index (0-based) derived from the connected device name, or null. */
  playerId: number | null;
  gameState: BoardState | null;
  startScan: () => void;
  stopScan: () => void;
  connect: (deviceId: string) => Promise<void>;
  disconnect: () => Promise<void>;
  sendAction: (action: PlayerAction) => Promise<void>;
  sendInput: (input: Partial<PlayerInput> & { action: PlayerAction }) => Promise<void>;
  sendReport: (vp: number, resources: ResourceCounts) => Promise<void>;
}

const BleContext = createContext<BleContextValue | null>(null);

/** Parses a device name like "Catan-P3" → player index 2 (0-based). Null if unknown. */
function parsePlayerIdFromName(name: string | null | undefined): number | null {
  if (!name) return null;
  const m = /^Catan-P(\d+)$/.exec(name);
  if (!m) return null;
  const n = parseInt(m[1], 10);
  if (!Number.isFinite(n) || n < 1 || n > 4) return null;
  return n - 1;
}

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
  const [playerId, setPlayerId] = useState<number | null>(null);
  const [gameState, setGameState] = useState<BoardState | null>(null);

  useEffect(() => {
    const mgr = new BleManager();
    managerRef.current = mgr;
    const stateSub = mgr.onStateChange(s => setBleState(s), true);
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
        if (device?.name && /^Catan-P\d+$/.test(device.name)) {
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
        setPlayerId(parsePlayerIdFromName(device.name));

        disconnectSubRef.current = device.onDisconnected(() => {
          notifSubRef.current?.remove();
          notifSubRef.current = null;
          disconnectSubRef.current = null;
          deviceRef.current = null;
          setConnectedName(null);
          setPlayerId(null);
          setGameState(null);
          setConnectionState('idle');
        });

        notifSubRef.current = device.monitorCharacteristicForService(
          CATAN_SERVICE_UUID,
          GAME_STATE_UUID,
          (err, characteristic) => {
            if (err || !characteristic?.value) return;
            const decoded = decodeBoardStateFrame(characteristic.value);
            if (decoded) setGameState(decoded);
          },
        );

        try {
          const char = await device.readCharacteristicForService(
            CATAN_SERVICE_UUID,
            GAME_STATE_UUID,
          );
          if (char.value) {
            const decoded = decodeBoardStateFrame(char.value);
            if (decoded) setGameState(decoded);
          }
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
    setPlayerId(null);
    setGameState(null);
    setConnectionState('idle');
  }, []);

  // ── Send helpers ─────────────────────────────────────────────────────────

  const writePayload = useCallback(async (payload: string) => {
    const device = deviceRef.current;
    if (!device) return;
    await device.writeCharacteristicWithResponseForService(
      CATAN_SERVICE_UUID,
      COMMAND_UUID,
      payload,
    );
  }, []);

  const sendAction = useCallback(
    async (action: PlayerAction) => {
      const id = playerId ?? 0;
      await writePayload(encodeAction(id, action));
    },
    [playerId, writePayload],
  );

  const sendInput = useCallback(
    async (input: Partial<PlayerInput> & { action: PlayerAction }) => {
      const id = input.playerId ?? playerId ?? 0;
      await writePayload(
        encodePlayerInput({
          protoVersion: input.protoVersion ?? 3,
          playerId: id,
          action: input.action,
          vp: input.vp,
          resLumber: input.resLumber,
          resWool: input.resWool,
          resGrain: input.resGrain,
          resBrick: input.resBrick,
          resOre: input.resOre,
        }),
      );
    },
    [playerId, writePayload],
  );

  const sendReport = useCallback(
    async (vp: number, resources: ResourceCounts) => {
      const id = playerId ?? 0;
      await writePayload(encodeReport(id, vp, resources));
    },
    [playerId, writePayload],
  );

  return (
    <BleContext.Provider
      value={{
        bleState,
        scanning,
        devices,
        connectionState,
        connectedName,
        playerId,
        gameState,
        startScan,
        stopScan,
        connect,
        disconnect,
        sendAction,
        sendInput,
        sendReport,
      }}>
      {children}
    </BleContext.Provider>
  );
}

export function useBle() {
  const ctx = useContext(BleContext);
  if (!ctx) throw new Error('useBle must be used within BleProvider');
  return ctx;
}
