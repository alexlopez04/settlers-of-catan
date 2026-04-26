// =============================================================================
// sim-board.ts — Simulated Catan board for offline / App Review testing.
//
// The simulated state mirrors the v7 BoardState shape so the UI can exercise
// every flow (purchases, robber/discard, dev cards, trades) without a hub.
// The reducer is intentionally lightweight: it focuses on phase progression
// and resource bookkeeping that the UI cares about. It is NOT a full
// rules engine — for that, talk to the Mega.
// =============================================================================

import {
  Biome,
  BoardState,
  CATAN_PROTO_VERSION,
  DEV_CARD_COUNT,
  DevCard,
  Difficulty,
  GamePhase,
  NO_PLAYER,
  NO_WINNER,
  PLAYER_COUNT,
  PlayerAction,
  PlayerInput,
  RESOURCE_COUNT,
  RejectReason,
  Resource,
} from '@/services/proto';
import { REVEAL_ORDER, ROAD_COST, SETTLEMENT_COST, CITY_COST, DEV_CARD_COST } from '@/constants/game';

// ── Fixed board layout (standard Catan distribution) ────────────────────────
const SIM_TILE_DATA: ReadonlyArray<{ biome: Biome; number: number }> = [
  { biome: Biome.FIELD,    number: 9  },
  { biome: Biome.MOUNTAIN, number: 10 },
  { biome: Biome.FOREST,   number: 2  },
  { biome: Biome.HILL,     number: 6  },
  { biome: Biome.DESERT,   number: 0  },
  { biome: Biome.FOREST,   number: 3  },
  { biome: Biome.PASTURE,  number: 8  },
  { biome: Biome.FIELD,    number: 11 },
  { biome: Biome.MOUNTAIN, number: 12 },
  { biome: Biome.FIELD,    number: 5  },
  { biome: Biome.HILL,     number: 5  },
  { biome: Biome.MOUNTAIN, number: 3  },
  { biome: Biome.PASTURE,  number: 4  },
  { biome: Biome.HILL,     number: 4  },
  { biome: Biome.FOREST,   number: 11 },
  { biome: Biome.PASTURE,  number: 10 },
  { biome: Biome.FOREST,   number: 9  },
  { biome: Biome.PASTURE,  number: 6  },
  { biome: Biome.FIELD,    number: 8  },
];

const ZERO4  = (): number[] => [0, 0, 0, 0];
const ZERO5  = (): number[] => [0, 0, 0, 0, 0];
const ZERO20 = (): number[] => Array.from({ length: 20 }, () => 0);

/** Returns the initial simulated BoardState (LOBBY, 1 player). */
export function createSimulatedState(): BoardState {
  return {
    protoVersion:     CATAN_PROTO_VERSION,
    phase:            GamePhase.LOBBY,
    numPlayers:       1,
    currentPlayer:    0,
    setupRound:       0,
    hasRolled:        false,
    die1:             0,
    die2:             0,
    revealNumber:     0,
    winnerId:         NO_WINNER,
    robberTile:       4,
    connectedMask:    0x01,
    tiles:            SIM_TILE_DATA.map(t => ({ biome: t.biome, number: t.number })),
    vp:               ZERO4(),
    ready:            ZERO4(),
    resLumber:        ZERO4(),
    resWool:          ZERO4(),
    resGrain:         ZERO4(),
    resBrick:         ZERO4(),
    resOre:           ZERO4(),
    vertices:         Array(54).fill(null),
    edges:            Array(72).fill(null),
    lastRejectReason: RejectReason.NONE,
    devCards:         ZERO20(),
    knightsPlayed:    ZERO4(),
    largestArmyPlayer: NO_PLAYER,
    longestRoadPlayer: NO_PLAYER,
    longestRoadLength: 0,
    devDeckRemaining: 25,
    cardPlayedThisTurn: false,
    discardRequiredMask: 0,
    discardRequiredCount: ZERO4(),
    stealEligibleMask: 0,
    pendingRoadBuy:       ZERO4(),
    pendingSettlementBuy: ZERO4(),
    pendingCityBuy:       ZERO4(),
    freeRoadsRemaining:   ZERO4(),
    trade: {
      fromPlayer: NO_PLAYER,
      toPlayer: NO_PLAYER,
      offer: ZERO5(),
      want: ZERO5(),
    },
    bankSupply:       [19, 19, 19, 19, 19],
    lastDistribution: ZERO20(),
    difficulty:       Difficulty.NORMAL,
    hasSavedGame:     false,
  };
}

// ── Reducer helpers ──────────────────────────────────────────────────────────

function totalCards(s: BoardState, p: number): number {
  return (
    (s.resLumber[p] ?? 0) +
    (s.resWool[p]   ?? 0) +
    (s.resGrain[p]  ?? 0) +
    (s.resBrick[p]  ?? 0) +
    (s.resOre[p]    ?? 0)
  );
}

function arrayWith<T>(arr: T[], i: number, v: T): T[] {
  const out = arr.slice();
  out[i] = v;
  return out;
}

function add(arr: number[], i: number, delta: number): number[] {
  return arrayWith(arr, i, (arr[i] ?? 0) + delta);
}

function spendFromPlayer(s: BoardState, p: number, cost: readonly number[]): BoardState {
  if ((s.resLumber[p] ?? 0) < cost[Resource.LUMBER]) return s;
  if ((s.resWool[p]   ?? 0) < cost[Resource.WOOL])   return s;
  if ((s.resGrain[p]  ?? 0) < cost[Resource.GRAIN])  return s;
  if ((s.resBrick[p]  ?? 0) < cost[Resource.BRICK])  return s;
  if ((s.resOre[p]    ?? 0) < cost[Resource.ORE])    return s;

  return {
    ...s,
    resLumber: add(s.resLumber, p, -cost[Resource.LUMBER]),
    resWool:   add(s.resWool,   p, -cost[Resource.WOOL]),
    resGrain:  add(s.resGrain,  p, -cost[Resource.GRAIN]),
    resBrick:  add(s.resBrick,  p, -cost[Resource.BRICK]),
    resOre:    add(s.resOre,    p, -cost[Resource.ORE]),
    bankSupply: s.bankSupply.map((b, i) => b + cost[i]),
  };
}

// ── Reducer ──────────────────────────────────────────────────────────────────

/**
 * Pure reducer: returns a new BoardState after applying the given input.
 * Only the subset of v7 actions actually exercised by the offline UI is
 * implemented here. Unrecognised actions are ignored.
 */
export function applySimulatedAction(
  state: BoardState,
  input: Partial<PlayerInput> & { action: PlayerAction },
): BoardState {
  let s: BoardState = { ...state };
  const action = input.action;
  const p = input.playerId ?? s.currentPlayer ?? 0;

  // Phase-driven progression.
  switch (s.phase) {
    case GamePhase.LOBBY:
      if (action === PlayerAction.START_GAME) {
        s.phase        = GamePhase.BOARD_SETUP;
        s.revealNumber = 0;
      }
      break;

    case GamePhase.BOARD_SETUP:
      if (action === PlayerAction.NEXT_NUMBER) {
        s.phase        = GamePhase.NUMBER_REVEAL;
        s.revealNumber = REVEAL_ORDER[0];
      }
      break;

    case GamePhase.NUMBER_REVEAL:
      if (action === PlayerAction.NEXT_NUMBER) {
        const idx = REVEAL_ORDER.indexOf(s.revealNumber);
        if (idx >= 0 && idx < REVEAL_ORDER.length - 1) {
          s.revealNumber = REVEAL_ORDER[idx + 1];
        } else {
          s.phase         = GamePhase.INITIAL_PLACEMENT;
          s.currentPlayer = 0;
          s.setupRound    = 0;
        }
      }
      break;

    case GamePhase.INITIAL_PLACEMENT:
      if (action === PlayerAction.PLACE_DONE) {
        if (s.setupRound === 0) {
          s.setupRound = 1;
        } else {
          s.phase         = GamePhase.PLAYING;
          s.currentPlayer = 0;
          s.hasRolled     = false;
        }
      }
      break;

    case GamePhase.PLAYING:
      if (action === PlayerAction.ROLL_DICE) {
        const die1 = Math.ceil(Math.random() * 6);
        const die2 = Math.ceil(Math.random() * 6);
        s.die1     = die1;
        s.die2     = die2;
        s.hasRolled = true;
        if (die1 + die2 === 7) {
          // Anyone with > 7 cards must discard first.
          let mask = 0;
          const counts = ZERO4();
          for (let i = 0; i < PLAYER_COUNT; i++) {
            const tc = totalCards(s, i);
            if (tc > 7) {
              mask |= 1 << i;
              counts[i] = Math.floor(tc / 2);
            }
          }
          if (mask !== 0) {
            s.phase = GamePhase.DISCARD;
            s.discardRequiredMask  = mask;
            s.discardRequiredCount = counts;
          } else {
            s.phase = GamePhase.ROBBER;
          }
        }
      } else if (action === PlayerAction.END_TURN && s.hasRolled) {
        s.currentPlayer        = (s.currentPlayer + 1) % Math.max(1, s.numPlayers);
        s.hasRolled            = false;
        s.die1                 = 0;
        s.die2                 = 0;
        s.cardPlayedThisTurn   = false;
        s.lastDistribution     = ZERO20();
        s.freeRoadsRemaining   = ZERO4();
      } else if (action === PlayerAction.BUY_ROAD) {
        const next = spendFromPlayer(s, p, ROAD_COST);
        if (next !== s) {
          s = next;
          s.pendingRoadBuy = add(s.pendingRoadBuy, p, 1);
        }
      } else if (action === PlayerAction.BUY_SETTLEMENT) {
        const next = spendFromPlayer(s, p, SETTLEMENT_COST);
        if (next !== s) {
          s = next;
          s.pendingSettlementBuy = add(s.pendingSettlementBuy, p, 1);
        }
      } else if (action === PlayerAction.BUY_CITY) {
        const next = spendFromPlayer(s, p, CITY_COST);
        if (next !== s) {
          s = next;
          s.pendingCityBuy = add(s.pendingCityBuy, p, 1);
        }
      } else if (action === PlayerAction.BUY_DEV_CARD && s.devDeckRemaining > 0) {
        const next = spendFromPlayer(s, p, DEV_CARD_COST);
        if (next !== s) {
          s = next;
          // Hand out a Knight by default (sim only).
          const idx = p * DEV_CARD_COUNT + DevCard.KNIGHT;
          s.devCards = arrayWith(s.devCards, idx, (s.devCards[idx] ?? 0) + 1);
          s.devDeckRemaining -= 1;
        }
      } else if (action === PlayerAction.BANK_TRADE) {
        // Simple 4:1 — no port awareness in the sim.
        const give = [
          input.resLumber ?? 0,
          input.resWool   ?? 0,
          input.resGrain  ?? 0,
          input.resBrick  ?? 0,
          input.resOre    ?? 0,
        ];
        const want = [
          input.wantLumber ?? 0,
          input.wantWool   ?? 0,
          input.wantGrain  ?? 0,
          input.wantBrick  ?? 0,
          input.wantOre    ?? 0,
        ];
        const giveTotal = give.reduce((a, b) => a + b, 0);
        const wantTotal = want.reduce((a, b) => a + b, 0);
        if (giveTotal > 0 && wantTotal > 0 && giveTotal === wantTotal * 4) {
          const next = spendFromPlayer(s, p, give);
          if (next !== s) {
            s = next;
            s.resLumber = add(s.resLumber, p, want[0]);
            s.resWool   = add(s.resWool,   p, want[1]);
            s.resGrain  = add(s.resGrain,  p, want[2]);
            s.resBrick  = add(s.resBrick,  p, want[3]);
            s.resOre    = add(s.resOre,    p, want[4]);
            s.bankSupply = s.bankSupply.map((b, i) => b - want[i]);
          }
        }
      }
      break;

    case GamePhase.DISCARD:
      if (action === PlayerAction.DISCARD) {
        const counts = [
          input.resLumber ?? 0,
          input.resWool   ?? 0,
          input.resGrain  ?? 0,
          input.resBrick  ?? 0,
          input.resOre    ?? 0,
        ];
        const total = counts.reduce((a, b) => a + b, 0);
        if (total === (s.discardRequiredCount[p] ?? 0) && total > 0) {
          const next = spendFromPlayer(s, p, counts);
          if (next !== s) {
            s = next;
            const newMask = s.discardRequiredMask & ~(1 << p);
            s.discardRequiredMask  = newMask;
            s.discardRequiredCount = arrayWith(s.discardRequiredCount, p, 0);
            if (newMask === 0) s.phase = GamePhase.ROBBER;
          }
        }
      }
      break;

    case GamePhase.ROBBER:
      if (action === PlayerAction.PLACE_ROBBER && input.robberTile !== undefined) {
        s.robberTile = input.robberTile;
        // Sim has no per-player vertex map; just exit robber.
        s.phase = GamePhase.PLAYING;
      } else if (action === PlayerAction.SKIP_ROBBER) {
        s.phase = GamePhase.PLAYING;
      } else if (action === PlayerAction.STEAL_FROM) {
        s.phase = GamePhase.PLAYING;
      }
      break;

    default:
      break;
  }

  return s;
}

// Re-export RESOURCE_COUNT for callers (tree-shake friendly placeholder).
export const SIM_RESOURCE_COUNT = RESOURCE_COUNT;
