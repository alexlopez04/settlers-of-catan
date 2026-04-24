// =============================================================================
// sim-board.ts — Simulated Catan board for debug / App Review testing.
//
// Provides a fixed board layout and a pure reducer that advances game state
// in response to player actions without any BLE hardware.
// =============================================================================

import {
  Biome,
  BoardState,
  GamePhase,
  PlayerAction,
  RejectReason,
  NO_WINNER,
  CATAN_PROTO_VERSION,
} from '@/services/proto';
import type { ResourceCounts } from '@/context/ble-context';
import { REVEAL_ORDER } from '@/constants/game';

// ── Fixed board layout (standard Catan distribution) ────────────────────────
//
// Tile order matches TILE_HEX_COORDS in board-topology.ts.
//   4× Forest  4× Pasture  4× Field  3× Hill  3× Mountain  1× Desert
//   Numbers: 2,3,3,4,4,5,5,6,6,8,8,9,9,10,10,11,11,12 + 0 (desert)

const SIM_TILE_DATA: ReadonlyArray<{ biome: Biome; number: number }> = [
  { biome: Biome.FIELD,    number: 9  }, // T00
  { biome: Biome.MOUNTAIN, number: 10 }, // T01
  { biome: Biome.FOREST,   number: 2  }, // T02
  { biome: Biome.HILL,     number: 6  }, // T03
  { biome: Biome.DESERT,   number: 0  }, // T04 — desert / robber start
  { biome: Biome.FOREST,   number: 3  }, // T05
  { biome: Biome.PASTURE,  number: 8  }, // T06
  { biome: Biome.FIELD,    number: 11 }, // T07
  { biome: Biome.MOUNTAIN, number: 12 }, // T08
  { biome: Biome.FIELD,    number: 5  }, // T09
  { biome: Biome.HILL,     number: 5  }, // T10
  { biome: Biome.MOUNTAIN, number: 3  }, // T11
  { biome: Biome.PASTURE,  number: 4  }, // T12
  { biome: Biome.HILL,     number: 4  }, // T13
  { biome: Biome.FOREST,   number: 11 }, // T14
  { biome: Biome.PASTURE,  number: 10 }, // T15
  { biome: Biome.FOREST,   number: 9  }, // T16
  { biome: Biome.PASTURE,  number: 6  }, // T17
  { biome: Biome.FIELD,    number: 8  }, // T18
];

// ── Factory ──────────────────────────────────────────────────────────────────

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
    robberTile:       4, // desert
    connectedMask:    0x01, // player 0 connected
    tiles:            SIM_TILE_DATA.map(t => ({ biome: t.biome, number: t.number })),
    vp:               [0, 0, 0, 0],
    ready:            [0, 0, 0, 0],
    resLumber:        [0, 0, 0, 0],
    resWool:          [0, 0, 0, 0],
    resGrain:         [0, 0, 0, 0],
    resBrick:         [0, 0, 0, 0],
    resOre:           [0, 0, 0, 0],
    vertices:         Array(54).fill(null),
    edges:            Array(72).fill(null),
    lastRejectReason: RejectReason.NONE,
  };
}

// ── State machine ────────────────────────────────────────────────────────────

/**
 * Pure reducer: returns a new BoardState after applying `action`.
 * `vp` and `resources` are only used for REPORT actions.
 */
export function applySimulatedAction(
  state: BoardState,
  action: PlayerAction,
  vp?: number,
  resources?: ResourceCounts,
): BoardState {
  // Shallow-clone top-level so callers always get a new reference.
  const s: BoardState = { ...state };

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
        s.revealNumber = REVEAL_ORDER[0]; // 2
      }
      break;

    case GamePhase.NUMBER_REVEAL:
      if (action === PlayerAction.NEXT_NUMBER) {
        const revealIdx = REVEAL_ORDER.indexOf(s.revealNumber);
        if (revealIdx >= 0 && revealIdx < REVEAL_ORDER.length - 1) {
          s.revealNumber = REVEAL_ORDER[revealIdx + 1];
        } else {
          // All 10 number tokens revealed → initial placement.
          s.phase         = GamePhase.INITIAL_PLACEMENT;
          s.currentPlayer = 0;
          s.setupRound    = 0;
        }
      }
      break;

    case GamePhase.INITIAL_PLACEMENT:
      // With 1 simulated player: two rounds (setupRound 0 and 1) then → PLAYING.
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
          s.phase = GamePhase.ROBBER;
        }
      } else if (action === PlayerAction.END_TURN && s.hasRolled) {
        s.currentPlayer = (s.currentPlayer + 1) % s.numPlayers;
        s.hasRolled     = false;
        s.die1          = 0;
        s.die2          = 0;
      }
      break;

    case GamePhase.ROBBER:
      if (action === PlayerAction.SKIP_ROBBER) {
        s.phase = GamePhase.PLAYING;
      }
      break;

    default:
      break;
  }

  // REPORT: update VP and resources (fired by game.tsx on any change).
  if (action === PlayerAction.REPORT && vp !== undefined) {
    const newVp = [...s.vp];
    newVp[0]   = vp;
    s.vp       = newVp;
    if (vp >= 10 && s.phase !== GamePhase.GAME_OVER) {
      s.phase    = GamePhase.GAME_OVER;
      s.winnerId = 0;
    }
  }

  if (resources) {
    s.resLumber        = [...s.resLumber]; s.resLumber[0] = resources.lumber;
    s.resWool          = [...s.resWool];   s.resWool[0]   = resources.wool;
    s.resGrain         = [...s.resGrain];  s.resGrain[0]  = resources.grain;
    s.resBrick         = [...s.resBrick];  s.resBrick[0]  = resources.brick;
    s.resOre           = [...s.resOre];    s.resOre[0]    = resources.ore;
  }

  return s;
}
