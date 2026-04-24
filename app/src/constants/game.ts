import { GamePhase, PlayerAction } from '@/services/proto';
import type { ResourceCounts } from '@/context/ble-context';

// ── Phase display labels ──────────────────────────────────────────────────

export const PHASE_LABEL: Record<GamePhase, string> = {
  [GamePhase.LOBBY]: 'Lobby',
  [GamePhase.BOARD_SETUP]: 'Board Setup',
  [GamePhase.NUMBER_REVEAL]: 'Number Reveal',
  [GamePhase.INITIAL_PLACEMENT]: 'Initial Placement',
  [GamePhase.PLAYING]: 'Playing',
  [GamePhase.ROBBER]: 'Robber',
  [GamePhase.GAME_OVER]: 'Game Over',
};

// ── Resource definitions ──────────────────────────────────────────────────

export type ResKey = keyof ResourceCounts;

export const RESOURCES: { key: ResKey; emoji: string; label: string }[] = [
  { key: 'lumber', emoji: '🪵', label: 'Lumber' },
  { key: 'wool',   emoji: '🐑', label: 'Wool'   },
  { key: 'grain',  emoji: '🌾', label: 'Grain'  },
  { key: 'brick',  emoji: '🧱', label: 'Brick'  },
  { key: 'ore',    emoji: '⛏',  label: 'Ore'    },
];

// ── Dice ──────────────────────────────────────────────────────────────────

export const DIE_FACES = ['', '⚀', '⚁', '⚂', '⚃', '⚄', '⚅'];

// ── Number reveal order ───────────────────────────────────────────────────

export const REVEAL_ORDER = [2, 3, 4, 5, 6, 8, 9, 10, 11, 12];

// ── Button spec ───────────────────────────────────────────────────────────

export interface ButtonSpec {
  label: string;
  sfSymbol?: string;
  action: PlayerAction;
  enabled: boolean;
  primary?: boolean;
}

/**
 * Returns only the buttons relevant for the current game state.
 * Buttons always expand to fill the bar (no placeholders).
 */
export function buttonsForPhase(
  phase: GamePhase,
  myTurn: boolean,
  hasRolled: boolean,
  connectedCount: number,
): ButtonSpec[] {
  switch (phase) {
    case GamePhase.LOBBY: {
      const canStart = connectedCount >= 1;
      const btns: ButtonSpec[] = [
        { label: 'Ready', sfSymbol: 'checkmark.circle', action: PlayerAction.READY, enabled: true },
      ];
      if (canStart) {
        btns.push({ label: 'Start Game', sfSymbol: 'play.fill', action: PlayerAction.START_GAME, enabled: true, primary: true });
      }
      return btns;
    }
    case GamePhase.BOARD_SETUP:
      return [{ label: 'Start Reveal', sfSymbol: 'arrow.right.circle.fill', action: PlayerAction.NEXT_NUMBER, enabled: true, primary: true }];
    case GamePhase.NUMBER_REVEAL:
      return [{ label: 'Next Number', sfSymbol: 'arrow.right.circle.fill', action: PlayerAction.NEXT_NUMBER, enabled: true, primary: true }];
    case GamePhase.INITIAL_PLACEMENT:
      return [{ label: 'Placement Done', sfSymbol: 'checkmark.circle.fill', action: PlayerAction.PLACE_DONE, enabled: myTurn, primary: myTurn }];
    case GamePhase.PLAYING: {
      if (!hasRolled) {
        return [{ label: 'Roll Dice', sfSymbol: 'die.face.5.fill', action: PlayerAction.ROLL_DICE, enabled: myTurn, primary: myTurn }];
      }
      return [{ label: 'End Turn', sfSymbol: 'arrow.trianglehead.clockwise', action: PlayerAction.END_TURN, enabled: myTurn, primary: myTurn }];
    }
    case GamePhase.ROBBER:
      return [{ label: 'Skip Robber', sfSymbol: 'forward.fill', action: PlayerAction.SKIP_ROBBER, enabled: myTurn, primary: myTurn }];
    default:
      return [];
  }
}
