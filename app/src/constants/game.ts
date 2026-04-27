import { GamePhase, PlayerAction, Resource } from '@/services/proto';
import type { ResourceCounts } from '@/context/ble-context';

// ── Phase display labels ──────────────────────────────────────────────────

export const PHASE_LABEL: Record<GamePhase, string> = {
  [GamePhase.LOBBY]: 'Lobby',
  [GamePhase.BOARD_SETUP]: 'Board Setup',
  [GamePhase.NUMBER_REVEAL]: 'Number Reveal',
  [GamePhase.INITIAL_PLACEMENT]: 'Initial Placement',
  [GamePhase.PLAYING]: 'Playing',
  [GamePhase.ROBBER]: 'Robber',
  [GamePhase.DISCARD]: 'Discard',
  [GamePhase.GAME_OVER]: 'Game Over',
};

// ── Resource definitions ──────────────────────────────────────────────────

export type ResKey = keyof ResourceCounts;

export const RESOURCES: { key: ResKey; emoji: string; label: string; index: Resource }[] = [
  { key: 'lumber', emoji: '🪵', label: 'Lumber', index: Resource.LUMBER },
  { key: 'wool',   emoji: '🐑', label: 'Wool',   index: Resource.WOOL   },
  { key: 'grain',  emoji: '🌾', label: 'Grain',  index: Resource.GRAIN  },
  { key: 'brick',  emoji: '🧱', label: 'Brick',  index: Resource.BRICK  },
  { key: 'ore',    emoji: '⛏',  label: 'Ore',    index: Resource.ORE    },
];

// ── Dice ──────────────────────────────────────────────────────────────────

export const DIE_FACES = ['', '⚀', '⚁', '⚂', '⚃', '⚄', '⚅'];

// ── Number reveal order ───────────────────────────────────────────────────

export const REVEAL_ORDER = [2, 3, 4, 5, 6, 8, 9, 10, 11, 12];

// ── Build costs (indexed by Resource: L,W,G,B,O) ─────────────────────────

export const ROAD_COST       = [1, 0, 0, 1, 0] as const;
export const SETTLEMENT_COST = [1, 1, 1, 1, 0] as const;
export const CITY_COST       = [0, 0, 2, 0, 3] as const;
export const DEV_CARD_COST   = [0, 1, 1, 0, 1] as const;

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
 * @param hasPendingTrade - when true, END_TURN is disabled until the open
 *   player-to-player trade offer is resolved (accepted, declined, or cancelled).
 */
export function buttonsForPhase(
  phase: GamePhase,
  myTurn: boolean,
  hasRolled: boolean,
  connectedCount: number,
  myId: number,
  hasPendingTrade?: boolean,
): ButtonSpec[] {
  switch (phase) {
    case GamePhase.LOBBY: {
      if (myId !== 0) return [];
      return [
        { label: 'Start Game', sfSymbol: 'play.fill', action: PlayerAction.START_GAME, enabled: connectedCount >= 1, primary: true },
      ];
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
      // Block End Turn while a player-to-player trade offer is pending.
      const canEndTurn = myTurn && !hasPendingTrade;
      return [{ label: 'End Turn', sfSymbol: 'arrow.trianglehead.clockwise', action: PlayerAction.END_TURN, enabled: canEndTurn, primary: canEndTurn }];
    }
    case GamePhase.ROBBER:
      // Robber tile picking is handled in a modal; provide Skip when applicable.
      return [{ label: 'Skip Steal', sfSymbol: 'forward.fill', action: PlayerAction.SKIP_ROBBER, enabled: myTurn, primary: myTurn }];
    case GamePhase.DISCARD:
      // Discard counts are entered via a modal; no inline buttons.
      return [];
    default:
      return [];
  }
}
