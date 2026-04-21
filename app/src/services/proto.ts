// =============================================================================
// proto.ts — Catan v3 wire protocol: BoardState / PlayerInput encode/decode.
//
// Wire frame on every hop (including BLE):
//
//   [ 0xCA magic ] [ len : uint8 ] [ nanopb payload : len bytes ]
//
// BLE characteristic values from react-native-ble-plx are base64-encoded.
// =============================================================================

// ── Wire constants (must match firmware/{*}/src/catan_wire.h) ────────────
export const CATAN_WIRE_MAGIC = 0xca;
export const CATAN_FRAME_HEADER = 2;
export const CATAN_PROTO_VERSION = 3;

// ── Enums (mirror catan.proto) ────────────────────────────────────────────

export enum GamePhase {
  LOBBY = 0,
  BOARD_SETUP = 1,
  NUMBER_REVEAL = 2,
  INITIAL_PLACEMENT = 3,
  PLAYING = 4,
  ROBBER = 5,
  GAME_OVER = 6,
}

export enum PlayerAction {
  NONE = 0,
  READY = 1,
  START_GAME = 2,
  NEXT_NUMBER = 3,
  PLACE_DONE = 4,
  ROLL_DICE = 5,
  END_TURN = 6,
  SKIP_ROBBER = 7,
  REPORT = 8,
}

export enum Biome {
  DESERT = 0,
  FOREST = 1,
  PASTURE = 2,
  FIELD = 3,
  HILL = 4,
  MOUNTAIN = 5,
}

// ── Message types ─────────────────────────────────────────────────────────

export interface Tile {
  biome: Biome;
  number: number;
}

export interface BoardState {
  protoVersion: number;
  phase: GamePhase;
  numPlayers: number;
  currentPlayer: number;
  setupRound: number;
  hasRolled: boolean;
  die1: number;
  die2: number;
  revealNumber: number;
  winnerId: number;
  robberTile: number;
  connectedMask: number;
  tiles: Tile[];
  vp: number[];
}

export interface PlayerInput {
  protoVersion: number;
  playerId: number;
  action: PlayerAction;
  vp?: number;
  resLumber?: number;
  resWool?: number;
  resGrain?: number;
  resBrick?: number;
  resOre?: number;
}

export const NO_WINNER = 0xff;
export const NO_TILE = 0xff;

// ── Proto3 varint helpers ─────────────────────────────────────────────────

function readVarint(buf: Uint8Array, offset: number): [number, number] {
  let result = 0;
  let shift = 0;
  let b: number;
  do {
    b = buf[offset++];
    result |= (b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  return [result >>> 0, offset];
}

function writeVarint(value: number, out: number[]): void {
  let v = value >>> 0;
  while (v > 0x7f) {
    out.push((v & 0x7f) | 0x80);
    v >>>= 7;
  }
  out.push(v & 0x7f);
}

function encodeVarintField(fieldNum: number, value: number, out: number[]): void {
  if (!value) return; // proto3 default — omit zero values
  writeVarint((fieldNum << 3) | 0, out);
  writeVarint(value, out);
}

// ── Base64 ↔ Uint8Array ───────────────────────────────────────────────────

function base64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

function bytesToBase64(bytes: Uint8Array | number[]): string {
  let bin = '';
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

// ── Frame helpers ─────────────────────────────────────────────────────────

/** Wraps a nanopb payload in the Catan wire frame, returning base64. */
function frameBase64(payload: number[]): string {
  if (payload.length > 0xff) throw new Error(`payload too big: ${payload.length}`);
  const framed = [CATAN_WIRE_MAGIC, payload.length & 0xff, ...payload];
  return bytesToBase64(framed);
}

/** Strips the Catan wire frame, returning the nanopb payload or null. */
function unframe(bytes: Uint8Array): Uint8Array | null {
  if (bytes.length < CATAN_FRAME_HEADER) return null;
  if (bytes[0] !== CATAN_WIRE_MAGIC) return null;
  const len = bytes[1];
  if (len === 0 || CATAN_FRAME_HEADER + len > bytes.length) return null;
  return bytes.slice(CATAN_FRAME_HEADER, CATAN_FRAME_HEADER + len);
}

// ── Encode PlayerInput ────────────────────────────────────────────────────

/** Encodes and frames a PlayerInput message, returning a base64 string
 *  ready for writeCharacteristicWithResponseForService. */
export function encodePlayerInput(input: PlayerInput): string {
  const out: number[] = [];
  encodeVarintField(1, input.protoVersion || CATAN_PROTO_VERSION, out);
  encodeVarintField(2, input.playerId | 0, out);
  encodeVarintField(3, input.action | 0, out);
  if (input.vp)         encodeVarintField(10, input.vp | 0, out);
  if (input.resLumber)  encodeVarintField(11, input.resLumber | 0, out);
  if (input.resWool)    encodeVarintField(12, input.resWool | 0, out);
  if (input.resGrain)   encodeVarintField(13, input.resGrain | 0, out);
  if (input.resBrick)   encodeVarintField(14, input.resBrick | 0, out);
  if (input.resOre)     encodeVarintField(15, input.resOre | 0, out);
  return frameBase64(out);
}

/** Convenience helper for simple action-only inputs. */
export function encodeAction(playerId: number, action: PlayerAction): string {
  return encodePlayerInput({
    protoVersion: CATAN_PROTO_VERSION,
    playerId,
    action,
  });
}

/** Self-report current VP + resource counts. */
export function encodeReport(
  playerId: number,
  vp: number,
  resources: { lumber: number; wool: number; grain: number; brick: number; ore: number },
): string {
  return encodePlayerInput({
    protoVersion: CATAN_PROTO_VERSION,
    playerId,
    action: PlayerAction.REPORT,
    vp,
    resLumber: resources.lumber,
    resWool: resources.wool,
    resGrain: resources.grain,
    resBrick: resources.brick,
    resOre: resources.ore,
  });
}

// ── Decode BoardState ─────────────────────────────────────────────────────

function emptyBoardState(): BoardState {
  return {
    protoVersion: 0,
    phase: GamePhase.LOBBY,
    numPlayers: 0,
    currentPlayer: 0,
    setupRound: 0,
    hasRolled: false,
    die1: 0,
    die2: 0,
    revealNumber: 0,
    winnerId: NO_WINNER,
    robberTile: NO_TILE,
    connectedMask: 0,
    tiles: [],
    vp: [0, 0, 0, 0],
  };
}

function unpackTiles(packed: Uint8Array): Tile[] {
  const tiles: Tile[] = [];
  for (let i = 0; i < packed.length; i++) {
    const byte = packed[i];
    tiles.push({
      biome: ((byte >> 4) & 0x0f) as Biome,
      number: byte & 0x0f,
    });
  }
  return tiles;
}

/** Parses a base64 Catan frame (wire header + BoardState) into a state object. */
export function decodeBoardStateFrame(base64: string): BoardState | null {
  const bytes = base64ToBytes(base64);
  const payload = unframe(bytes);
  if (!payload) return null;
  return decodeBoardStatePayload(payload);
}

/** Parses a raw BoardState nanopb payload (no frame). */
export function decodeBoardStatePayload(buf: Uint8Array): BoardState {
  const state = emptyBoardState();
  let offset = 0;
  while (offset < buf.length) {
    let tag: number;
    [tag, offset] = readVarint(buf, offset);
    const fieldNum = tag >>> 3;
    const wireType = tag & 0x7;

    if (wireType === 0) {
      let v: number;
      [v, offset] = readVarint(buf, offset);
      switch (fieldNum) {
        case 1:  state.protoVersion = v; break;
        case 2:  state.phase = v as GamePhase; break;
        case 3:  state.numPlayers = v; break;
        case 4:  state.currentPlayer = v; break;
        case 5:  state.setupRound = v; break;
        case 6:  state.hasRolled = v !== 0; break;
        case 7:  state.die1 = v; break;
        case 8:  state.die2 = v; break;
        case 9:  state.revealNumber = v; break;
        case 10: state.winnerId = v; break;
        case 11: state.robberTile = v; break;
        case 12: state.connectedMask = v; break;
      }
    } else if (wireType === 2) {
      let len: number;
      [len, offset] = readVarint(buf, offset);
      const chunk = buf.slice(offset, offset + len);
      offset += len;
      switch (fieldNum) {
        case 13: state.tiles = unpackTiles(chunk); break;
        case 14: {
          // Packed repeated uint32 — decode each element.
          const vps: number[] = [];
          let o = 0;
          while (o < chunk.length) {
            let v: number;
            [v, o] = readVarint(chunk, o);
            vps.push(v);
          }
          // Always expose exactly 4 slots (missing = 0)
          state.vp = [vps[0] ?? 0, vps[1] ?? 0, vps[2] ?? 0, vps[3] ?? 0];
          break;
        }
      }
    } else {
      break;
    }
  }
  return state;
}

// Back-compat alias for callers that already use decodeBoardToPlayer name.
export const decodeBoardState = decodeBoardStateFrame;
