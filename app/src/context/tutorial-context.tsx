// =============================================================================
// tutorial-context.tsx — First-play tutorial state.
//
// Tracks which tutorial steps the player has already seen and persists that
// to AsyncStorage so the tutorial is only shown once per device.  If the
// debug toggle "alwaysOfferTutorial" is enabled the seen-set is ignored and
// every step fires again every game.
// =============================================================================

import AsyncStorage from '@react-native-async-storage/async-storage';
import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
} from 'react';

import { useSettings } from '@/context/settings-context';

// ── Step registry ────────────────────────────────────────────────────────────

export const TUTORIAL_STEP_IDS = [
  'welcome',           // LOBBY — greeting
  'board_setup',       // BOARD_SETUP — tiles assembling
  'number_reveal',     // NUMBER_REVEAL — token placement
  'initial_placement', // INITIAL_PLACEMENT — first settlements
  'first_roll',        // PLAYING — how to roll
  'use_resources',     // PLAYING — after first roll: build & trade
  'robber',            // ROBBER — what a 7 means
] as const;

export type TutorialStepId = typeof TUTORIAL_STEP_IDS[number];

export const TUTORIAL_TOTAL = TUTORIAL_STEP_IDS.length;

// ── Storage key ──────────────────────────────────────────────────────────────

const STORAGE_KEY = 'catan_tutorial_seen_v1';

// ── Context type ─────────────────────────────────────────────────────────────

interface TutorialContextValue {
  /**
   * Returns true when the step should be presented to the player.
   * False if they have already seen it (unless debug override is active).
   */
  shouldShowStep: (id: TutorialStepId) => boolean;
  /** Persist that the player has seen this step. */
  markStepSeen: (id: TutorialStepId) => void;
  /** Immediately mark every step as seen (player tapped "Skip Tutorial"). */
  skipAll: () => void;
  /** Clear all seen steps — used by the debug reset flow. */
  reset: () => void;
  /** 0-based index of `id` in TUTORIAL_STEP_IDS (for progress display). */
  stepIndex: (id: TutorialStepId) => number;
}

// ── Context ──────────────────────────────────────────────────────────────────

const TutorialContext = createContext<TutorialContextValue | null>(null);

export function TutorialProvider({ children }: { children: React.ReactNode }) {
  const { debug } = useSettings();
  const [seenSteps, setSeenSteps] = useState<ReadonlySet<TutorialStepId>>(new Set());

  // Load persisted seen-set on mount.
  useEffect(() => {
    AsyncStorage.getItem(STORAGE_KEY)
      .then(json => {
        if (!json) return;
        try {
          const arr = JSON.parse(json) as unknown[];
          const valid = arr.filter((s): s is TutorialStepId =>
            typeof s === 'string' &&
            (TUTORIAL_STEP_IDS as readonly string[]).includes(s),
          );
          setSeenSteps(new Set(valid));
        } catch { /* ignore malformed */ }
      })
      .catch(() => {});
  }, []);

  const persist = useCallback((set: ReadonlySet<TutorialStepId>) => {
    AsyncStorage.setItem(STORAGE_KEY, JSON.stringify([...set])).catch(() => {});
  }, []);

  const shouldShowStep = useCallback(
    (id: TutorialStepId): boolean => {
      if (debug.alwaysOfferTutorial) return true;
      return !seenSteps.has(id);
    },
    [seenSteps, debug.alwaysOfferTutorial],
  );

  const markStepSeen = useCallback(
    (id: TutorialStepId) => {
      setSeenSteps(prev => {
        if (prev.has(id)) return prev;
        const next = new Set(prev);
        next.add(id);
        persist(next);
        return next;
      });
    },
    [persist],
  );

  const skipAll = useCallback(() => {
    const all = new Set(TUTORIAL_STEP_IDS);
    setSeenSteps(all);
    persist(all);
  }, [persist]);

  const reset = useCallback(() => {
    setSeenSteps(new Set());
    AsyncStorage.removeItem(STORAGE_KEY).catch(() => {});
  }, []);

  const stepIndex = useCallback(
    (id: TutorialStepId) => TUTORIAL_STEP_IDS.indexOf(id),
    [],
  );

  return (
    <TutorialContext.Provider
      value={{ shouldShowStep, markStepSeen, skipAll, reset, stepIndex }}>
      {children}
    </TutorialContext.Provider>
  );
}

export function useTutorial(): TutorialContextValue {
  const ctx = useContext(TutorialContext);
  if (!ctx) throw new Error('useTutorial must be used within TutorialProvider');
  return ctx;
}
