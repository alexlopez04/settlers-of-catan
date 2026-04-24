import AsyncStorage from '@react-native-async-storage/async-storage';
import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
} from 'react';

// ── Debug flags ───────────────────────────────────────────────────────────────
//
// To add a new debug toggle:
//   1. Add a key + type here in DebugSettings.
//   2. Add its default value in DEFAULT_DEBUG.
//   3. Add a DebugToggleDef entry in settings.tsx's DEBUG_TOGGLES array.
//   That's it — the settings screen and persistence are fully automatic.

export interface DebugSettings {
  vertexOverlay: boolean;
  edgeOverlay: boolean;
  simulatedBoard: boolean;
}

const DEFAULT_DEBUG: DebugSettings = {
  vertexOverlay: false,
  edgeOverlay: false,
  simulatedBoard: false,
};

// ── Context ───────────────────────────────────────────────────────────────────

export interface SettingsContextValue {
  debug: DebugSettings;
  setDebug: <K extends keyof DebugSettings>(key: K, value: DebugSettings[K]) => void;
}

const SettingsContext = createContext<SettingsContextValue | null>(null);

const STORAGE_KEY = '@catan_settings_v1';

// ── Provider ──────────────────────────────────────────────────────────────────

export function SettingsProvider({ children }: { children: React.ReactNode }) {
  const [debug, setDebugState] = useState<DebugSettings>(DEFAULT_DEBUG);

  // Hydrate from storage on mount.
  useEffect(() => {
    AsyncStorage.getItem(STORAGE_KEY)
      .then(raw => {
        if (!raw) return;
        try {
          const saved = JSON.parse(raw);
          if (saved?.debug) {
            setDebugState(prev => ({ ...prev, ...saved.debug }));
          }
        } catch { /* corrupt data — ignore */ }
      })
      .catch(() => {});
  }, []);

  const setDebug = useCallback(
    <K extends keyof DebugSettings>(key: K, value: DebugSettings[K]) => {
      setDebugState(prev => {
        const next = { ...prev, [key]: value };
        AsyncStorage.setItem(STORAGE_KEY, JSON.stringify({ debug: next })).catch(() => {});
        return next;
      });
    },
    [],
  );

  return (
    <SettingsContext.Provider value={{ debug, setDebug }}>
      {children}
    </SettingsContext.Provider>
  );
}

// ── Hook ──────────────────────────────────────────────────────────────────────

export function useSettings(): SettingsContextValue {
  const ctx = useContext(SettingsContext);
  if (!ctx) throw new Error('useSettings must be used within a SettingsProvider');
  return ctx;
}
