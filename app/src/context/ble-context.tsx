import AsyncStorage from '@react-native-async-storage/async-storage';
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
  CATAN_DEVICE_NAME,
  CATAN_SERVICE_UUID,
  CLIENT_ID_STORAGE_KEY,
  COMMAND_UUID,
  GAME_STATE_UUID,
  IDENTITY_UUID,
  SCAN_TIMEOUT_MS,
  SLOT_UUID,
} from '@/constants/ble';
import {
  BoardState,
  NO_PLAYER,
  PlayerAction,
  PlayerInput,
  decodeBoardState,
  decodeSlot,
  encodeAction,
  encodeIdentity,
  encodePlayerInput,
} from '@/services/proto';
import { useSettings } from '@/context/settings-context';
import { applySimulatedAction, createSimulatedState } from '@/services/sim-board';

// ── Types ───────────────────────────────────────────────────────────────────

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
  /** Stable per-device identifier sent to the hub. */
  clientId: string | null;
  /** Player slot (0..3) assigned by the hub via the Slot characteristic. */
  playerId: number | null;
  gameState: BoardState | null;
  startScan: () => void;
  stopScan: () => void;
  connect: (deviceId: string) => Promise<void>;
  disconnect: () => Promise<void>;
  sendAction: (action: PlayerAction) => Promise<void>;
  sendInput: (input: Partial<PlayerInput> & { action: PlayerAction }) => Promise<void>;
  /** Immediately enter a fully-simulated game session (no BLE hardware needed). */
  connectSimulated: () => void;
}

const BleContext = createContext<BleContextValue | null>(null);

// ── Stable client identifier ────────────────────────────────────────────────

/** Generate an RFC4122 v4-ish UUID using Math.random (good enough for identity). */
function randomClientId(): string {
  const hex: string[] = [];
  for (let i = 0; i < 16; i++) hex.push(Math.floor(Math.random() * 256).toString(16).padStart(2, '0'));
  // version=4, variant=10xx
  hex[6] = ((parseInt(hex[6], 16) & 0x0f) | 0x40).toString(16).padStart(2, '0');
  hex[8] = ((parseInt(hex[8], 16) & 0x3f) | 0x80).toString(16).padStart(2, '0');
  return (
    hex.slice(0, 4).join('') +
    '-' + hex.slice(4, 6).join('') +
    '-' + hex.slice(6, 8).join('') +
    '-' + hex.slice(8, 10).join('') +
    '-' + hex.slice(10, 16).join('')
  );
}

async function loadOrCreateClientId(): Promise<string> {
  try {
    const existing = await AsyncStorage.getItem(CLIENT_ID_STORAGE_KEY);
    if (existing && existing.length > 0) return existing;
  } catch {
    /* fallthrough */
  }
  const fresh = randomClientId();
  try {
    await AsyncStorage.setItem(CLIENT_ID_STORAGE_KEY, fresh);
  } catch {
    /* non-fatal — id is still usable for this session */
  }
  return fresh;
}

// ── Provider ────────────────────────────────────────────────────────────────

export function BleProvider({ children }: { children: React.ReactNode }) {
  const managerRef = useRef<BleManager | null>(null);
  const deviceRef = useRef<Device | null>(null);
  const stateSubRef = useRef<Subscription | null>(null);
  const slotSubRef = useRef<Subscription | null>(null);
  const disconnectSubRef = useRef<Subscription | null>(null);
  const scanTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const clientIdRef = useRef<string | null>(null);
  /** True while running in simulated-board mode (no real BLE device). */
  const simulatedRef = useRef<boolean>(false);
  /** Mutable copy of game state used by simulated action handlers. */
  const simStateRef = useRef<BoardState | null>(null);

  const { debug } = useSettings();

  const [bleState, setBleState] = useState<State>(State.Unknown);
  const [scanning, setScanning] = useState(false);
  const [devices, setDevices] = useState<ScannedDevice[]>([]);
  const [connectionState, setConnectionState] = useState<ConnectionState>('idle');
  const [connectedName, setConnectedName] = useState<string | null>(null);
  const [clientId, setClientId] = useState<string | null>(null);
  const [playerId, setPlayerId] = useState<number | null>(null);
  const [gameState, setGameState] = useState<BoardState | null>(null);

  // Manager + persistent client_id boot
  useEffect(() => {
    const mgr = new BleManager();
    managerRef.current = mgr;
    const stateSub = mgr.onStateChange(s => {
      console.log('[BLE] state ->', s);
      setBleState(s);
    }, true);

    loadOrCreateClientId().then(id => {
      clientIdRef.current = id;
      setClientId(id);
    });

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
    if (!mgr || bleState !== State.PoweredOn) {
      console.log('[BLE] startScan skipped: mgr=', !!mgr, 'bleState=', bleState);
      return;
    }

    setDevices([]);
    setScanning(true);
    console.log('[BLE] startScan — scanning all devices (no OS-level UUID filter)');

    // Pass null to disable the OS-level service UUID filter.
    // CoreBluetooth on iOS silently drops devices that don't match the filter
    // even if the hub is nearby but advertising slightly differently. We do our
    // own matching in JS so we can log what is (and isn't) seen.
    mgr.startDeviceScan(
      null,
      { allowDuplicates: false },
      (error, device) => {
        if (error) {
          console.warn('[BLE] scan error:', JSON.stringify(error));
          setScanning(false);
          return;
        }
        if (!device) return;

        const deviceName = device.name ?? device.localName ?? null;
        const svcUUIDs   = (device.serviceUUIDs ?? []).map(u => u.toLowerCase());
        const targetUUID = CATAN_SERVICE_UUID.toLowerCase();
        const hasSvc     = svcUUIDs.includes(targetUUID);
        const hasName    = deviceName === CATAN_DEVICE_NAME;

        // Log every advertisement so we can see what's nearby.
        console.log(
          '[BLE] saw device:',
          device.id,
          'name=', deviceName,
          'svcUUIDs=', svcUUIDs,
          'hasSvc=', hasSvc,
          'hasName=', hasName,
          'rssi=', device.rssi,
        );

        if (!hasSvc && !hasName) return;

        setDevices(prev => {
          if (prev.some(d => d.id === device.id)) return prev;
          console.log('[BLE] ✓ matched Catan hub:', device.id, 'name=', deviceName);
          return [...prev, {
            id: device.id,
            name: deviceName ?? CATAN_DEVICE_NAME,
            rssi: device.rssi,
          }];
        });
      },
    );

    scanTimerRef.current = setTimeout(stopScan, SCAN_TIMEOUT_MS);
  }, [bleState, stopScan]);

  // ── Connect ─────────────────────────────────────────────────────────────

  const cleanupSubscriptions = useCallback(() => {
    stateSubRef.current?.remove(); stateSubRef.current = null;
    slotSubRef.current?.remove();  slotSubRef.current = null;
    disconnectSubRef.current?.remove(); disconnectSubRef.current = null;
  }, []);

  const connect = useCallback(
    async (deviceId: string) => {
      const mgr = managerRef.current;
      if (!mgr) return;

      // Need a stable client id before identifying ourselves to the hub.
      let cid = clientIdRef.current;
      if (!cid) {
        cid = await loadOrCreateClientId();
        clientIdRef.current = cid;
        setClientId(cid);
      }

      stopScan();
      setConnectionState('connecting');
      setPlayerId(null);

      try {
        console.log('[BLE] connecting to', deviceId, 'clientId=', cid);
        const device = await mgr.connectToDevice(deviceId, { requestMTU: 512 });
        console.log('[BLE] connected, discovering services...');
        await device.discoverAllServicesAndCharacteristics();
        console.log('[BLE] services discovered');

        deviceRef.current = device;
        setConnectedName(device.name ?? deviceId);

        disconnectSubRef.current = device.onDisconnected(() => {
          cleanupSubscriptions();
          deviceRef.current = null;
          setConnectedName(null);
          setPlayerId(null);
          setGameState(null);
          setConnectionState('idle');
        });

        // Subscribe to BoardState notifications.
        stateSubRef.current = device.monitorCharacteristicForService(
          CATAN_SERVICE_UUID,
          GAME_STATE_UUID,
          (err, characteristic) => {
            if (err) { console.warn('[BLE] State notify error:', JSON.stringify(err)); return; }
            if (!characteristic?.value) return;
            const decoded = decodeBoardState(characteristic.value);
            if (decoded) {
              console.log('[BLE] BoardState phase=', decoded.phase, 'players=', decoded.numPlayers);
              setGameState(decoded);
            } else {
              console.warn('[BLE] BoardState decode failed (proto version mismatch?)');
            }
          },
        );

        // Subscribe to Slot notifications BEFORE writing identity, so we don't
        // miss the assignment if the hub responds quickly.
        slotSubRef.current = device.monitorCharacteristicForService(
          CATAN_SERVICE_UUID,
          SLOT_UUID,
          (err, characteristic) => {
            if (err) { console.warn('[BLE] Slot notify error:', JSON.stringify(err)); return; }
            if (!characteristic?.value) return;
            const slot = decodeSlot(characteristic.value);
            console.log('[BLE] Slot notification: player_id=', slot);
            setPlayerId(slot === NO_PLAYER ? null : slot);
          },
        );

        // Identify ourselves; the hub will respond by notifying our slot.
        console.log('[BLE] writing Identity:', cid);
        await device.writeCharacteristicWithResponseForService(
          CATAN_SERVICE_UUID,
          IDENTITY_UUID,
          encodeIdentity(cid),
        );
        console.log('[BLE] Identity written');

        // Pull current Slot value in case the notification fired before we
        // subscribed.
        try {
          const slotChar = await device.readCharacteristicForService(
            CATAN_SERVICE_UUID,
            SLOT_UUID,
          );
          if (slotChar.value) {
            const slot = decodeSlot(slotChar.value);
            setPlayerId(slot === NO_PLAYER ? null : slot);
          }
        } catch {
          /* hub will notify */
        }

        // Pull initial BoardState (notifications follow).
        try {
          const stateChar = await device.readCharacteristicForService(
            CATAN_SERVICE_UUID,
            GAME_STATE_UUID,
          );
          if (stateChar.value) {
            const decoded = decodeBoardState(stateChar.value);
            if (decoded) setGameState(decoded);
          }
        } catch {
          /* notifications will populate */
        }

        setConnectionState('connected');
      } catch (err) {
        console.error('[BLE] connect failed:', JSON.stringify(err));
        cleanupSubscriptions();
        try { await mgr.cancelDeviceConnection(deviceId); } catch { /* ignore */ }
        deviceRef.current = null;
        setConnectionState('idle');
        throw err;
      }
    },
    [cleanupSubscriptions, stopScan],
  );

  // ── Disconnect ───────────────────────────────────────────────────────────

  const disconnect = useCallback(async () => {
    if (simulatedRef.current) {
      simulatedRef.current = false;
      simStateRef.current  = null;
      setConnectedName(null);
      setPlayerId(null);
      setGameState(null);
      setConnectionState('idle');
      return;
    }

    const device = deviceRef.current;
    setConnectionState('disconnecting');

    cleanupSubscriptions();

    try {
      await device?.cancelConnection();
    } catch {
      /* may already be disconnected */
    }

    deviceRef.current = null;
    setConnectedName(null);
    setPlayerId(null);
    setGameState(null);
    setConnectionState('idle');
  }, [cleanupSubscriptions]);

  // ── Simulated board ──────────────────────────────────────────────────────

  const connectSimulated = useCallback(() => {
    console.log('[SIM] entering simulated board mode');
    const initial = createSimulatedState();
    simulatedRef.current = true;
    simStateRef.current  = initial;
    setGameState(initial);
    setPlayerId(0);
    setConnectedName('Simulated Board');
    setConnectionState('connected');
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
      if (simulatedRef.current) {
        const prev = simStateRef.current;
        if (!prev) return;
        const next = applySimulatedAction(prev, { action });
        simStateRef.current = next;
        setGameState(next);
        console.log('[SIM] action', action, '→ phase', next.phase);
        return;
      }
      const id = playerId ?? 0;
      await writePayload(encodeAction(id, action, clientIdRef.current ?? undefined));
    },
    [playerId, writePayload],
  );

  const sendInput = useCallback(
    async (input: Partial<PlayerInput> & { action: PlayerAction }) => {
      if (simulatedRef.current) {
        const prev = simStateRef.current;
        if (!prev) return;
        const next = applySimulatedAction(prev, input);
        simStateRef.current = next;
        setGameState(next);
        return;
      }
      const id = input.playerId ?? playerId ?? 0;
      await writePayload(
        encodePlayerInput({
          ...input,
          playerId: id,
          action: input.action,
          clientId: input.clientId ?? clientIdRef.current ?? undefined,
        }),
      );
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
        clientId,
        playerId,
        gameState,
        startScan,
        stopScan,
        connect,
        disconnect,
        sendAction,
        sendInput,
        connectSimulated,
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
