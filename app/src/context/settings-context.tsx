// =============================================================================
// settings-context.tsx — App-wide settings: debug toggles + board rotation.
//
// All settings are persisted to AsyncStorage so they survive app restarts.
// =============================================================================

import AsyncStorage from '@react-native-async-storage/async-storage';
import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
} from 'react';

import { BoardRotation } from '@/utils/board-rotation';

// ── Types ────────────────────────────────────────────────────────────────────

export interface DebugSettings {
  /** Show vertex index numbers on the board map. */
  vertexOverlay: boolean;
  /** Show edge index numbers on the board map. */
  edgeOverlay: boolean;
  /** Show a "Simulated Board" option on the scan screen. */
  simulatedBoard: boolean;
}

interface SettingsContextValue {
  debug: DebugSettings;
  setDebug: (key: keyof DebugSettings, value: boolean) => void;
  /** Player's chosen board rotation (0–5, each step = 60° CW). */
  boardRotation: BoardRotation;
  setBoardRotation: (rotation: BoardRotation) => void;
}

// ── Defaults & storage keys ──────────────────────────────────────────────────

const DEFAULT_DEBUG: DebugSettings = {
  vertexOverlay: false,
  edgeOverlay:   false,
  simulatedBoard: false,
};

const STORAGE_KEY_DEBUG    = 'catan_debug_settings';
const STORAGE_KEY_ROTATION = 'catan_board_rotation';

// ── Context ──────────────────────────────────────────────────────────────────

const SettingsContext = createContext<SettingsContextValue | null>(null);

export function SettingsProvider({ children }: { children: React.ReactNode }) {
  const [debug,         setDebugState]         = useState<DebugSettings>(DEFAULT_DEBUG);
  const [boardRotation, setBoardRotationState] = useState<BoardRotation>(0);

  // Load persisted settings on mount.
  useEffect(() => {
    AsyncStorage.multiGet([STORAGE_KEY_DEBUG, STORAGE_KEY_ROTATION])
      .then(pairs => {
        const debugJson  = pairs[0][1];
        const rotStr     = pairs[1][1];

        if (debugJson) {
          try {
            const parsed = JSON.parse(debugJson) as Partial<DebugSettings>;
            setDebugState({ ...DEFAULT_DEBUG, ...parsed });
          } catch {
            /* ignore malformed JSON */
          }
        }

        if (rotStr) {
          const r = parseInt(rotStr, 10);
          if (r >= 0 && r <= 5) setBoardRotationState(r as BoardRotation);
        }
      })
      .catch(() => { /* non-fatal */ });
  }, []);

  const setDebug = useCallback((key: keyof DebugSettings, value: boolean) => {
    setDebugState(prev => {
      const next = { ...prev, [key]: value };
      AsyncStorage.setItem(STORAGE_KEY_DEBUG, JSON.stringify(next)).catch(() => {});
      return next;
    });
  }, []);

  const setBoardRotation = useCallback((rotation: BoardRotation) => {
    setBoardRotationState(rotation);
    AsyncStorage.setItem(STORAGE_KEY_ROTATION, String(rotation)).catch(() => {});
  }, []);

  return (
    <SettingsContext.Provider value={{ debug, setDebug, boardRotation, setBoardRotation }}>
      {children}
    </SettingsContext.Provider>
  );
}

export function useSettings(): SettingsContextValue {
  const ctx = useContext(SettingsContext);
  if (!ctx) throw new Error('useSettings must be used within SettingsProvider');
  return ctx;
}
