// =============================================================================
// proto.ts — Catan v6 wire protocol over BLE (BoardState + PlayerInput).
//
// On BLE there is NO framing envelope: the characteristic value IS the bare
// nanopb payload. The hub adds/strips the [0xCA][type][len][...][crc8] frame
// when forwarding to the Mega over UART; phones never see it.
//
//   hub -> mobile  (notify on State characteristic):    BoardState bytes
//   mobile -> hub  (write on Input characteristic):     PlayerInput bytes
//   mobile -> hub  (write once on Identity char):       client_id (UTF-8)
//   hub -> mobile  (read+notify on Slot characteristic): assigned player_id
//
// react-native-ble-plx delivers/accepts characteristic values as base64.
// =============================================================================

// ── Wire constants (must match firmware/*/src/proto/catan.pb.h) ─────────────
export const CATAN_PROTO_VERSION = 6;

// ── Enums (mirror catan.proto) ──────────────────────────────────────────────

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

// ── Public types ────────────────────────────────────────────────────────────

export interface Tile {
  biome: Biome;
  number: number;
}

/** Vertex ownership. Empty vertices are omitted from `vertices`. */
export interface VertexOwner {
  /** Player 0..3 */
  owner: number;
  /** true = city, false = settlement */
  city: boolean;
}

/** Edge (road) ownership. Empty edges are omitted from `edges`. */
export interface EdgeOwner {
  /** Player 0..3 */
  owner: number;
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
  /** 1 per player, 1 = ready in lobby. */
  ready: number[];
  /** Self-reported resource counts per player (index = player id). */
  resLumber: number[];
  resWool: number[];
  resGrain: number[];
  resBrick: number[];
  resOre: number[];
  /** Length 54; index = vertex id; null if empty. */
  vertices: (VertexOwner | null)[];
  /** Length 72; index = edge id; null if empty. */
  edges: (EdgeOwner | null)[];
}

export interface PlayerInput {
  playerId: number;
  action: PlayerAction;
  /** Optional UTF-8 client identifier (informational; hub re-stamps). */
  clientId?: string;
  vp?: number;
  resLumber?: number;
  resWool?: number;
  resGrain?: number;
  resBrick?: number;
  resOre?: number;
}

export const NO_WINNER = 0xff;
export const NO_TILE = 0xff;
export const NO_PLAYER = 0xff;

// ── Proto3 varint helpers ───────────────────────────────────────────────────

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

function utf8Bytes(s: string): number[] {
  const out: number[] = [];
  for (let i = 0; i < s.length; i++) {
    let c = s.charCodeAt(i);
    if (c < 0x80) {
      out.push(c);
    } else if (c < 0x800) {
      out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
    } else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) {
      const c2 = s.charCodeAt(++i);
      const cp = 0x10000 + (((c & 0x3ff) << 10) | (c2 & 0x3ff));
      out.push(
        0xf0 | (cp >> 18),
        0x80 | ((cp >> 12) & 0x3f),
        0x80 | ((cp >> 6) & 0x3f),
        0x80 | (cp & 0x3f),
      );
    } else {
      out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
    }
  }
  return out;
}

function encodeStringField(fieldNum: number, value: string, out: number[]): void {
  if (!value) return;
  const bytes = utf8Bytes(value);
  writeVarint((fieldNum << 3) | 2, out);
  writeVarint(bytes.length, out);
  for (const b of bytes) out.push(b);
}

// ── Base64 ⇄ Uint8Array ─────────────────────────────────────────────────────

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

// ── PlayerInput encode ──────────────────────────────────────────────────────
// Field tags: proto_version=1, player_id=2, action=3, client_id=4,
//             vp=10, res_*=11..15.

function encodePlayerInputBody(input: PlayerInput): number[] {
  const out: number[] = [];
  encodeVarintField(1, CATAN_PROTO_VERSION, out);
  encodeVarintField(2, input.playerId | 0, out);
  encodeVarintField(3, input.action | 0, out);
  if (input.clientId)  encodeStringField(4, input.clientId, out);
  if (input.vp)        encodeVarintField(10, input.vp | 0, out);
  if (input.resLumber) encodeVarintField(11, input.resLumber | 0, out);
  if (input.resWool)   encodeVarintField(12, input.resWool | 0, out);
  if (input.resGrain)  encodeVarintField(13, input.resGrain | 0, out);
  if (input.resBrick)  encodeVarintField(14, input.resBrick | 0, out);
  if (input.resOre)    encodeVarintField(15, input.resOre | 0, out);
  return out;
}

export function encodePlayerInput(input: PlayerInput): string {
  return bytesToBase64(encodePlayerInputBody(input));
}

export function encodeAction(
  playerId: number,
  action: PlayerAction,
  clientId?: string,
): string {
  return encodePlayerInput({ playerId, action, clientId });
}

export function encodeReport(
  playerId: number,
  vp: number,
  resources: { lumber: number; wool: number; grain: number; brick: number; ore: number },
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.REPORT,
    clientId,
    vp,
    resLumber: resources.lumber,
    resWool: resources.wool,
    resGrain: resources.grain,
    resBrick: resources.brick,
    resOre: resources.ore,
  });
}

/** Encode a UTF-8 string for the Identity characteristic (bare bytes). */
export function encodeIdentity(clientId: string): string {
  return bytesToBase64(utf8Bytes(clientId));
}

/** Decode a single-byte Slot characteristic value into a player_id (0..3 or NO_PLAYER). */
export function decodeSlot(base64: string): number {
  const bytes = base64ToBytes(base64);
  if (bytes.length === 0) return NO_PLAYER;
  return bytes[0];
}

// ── BoardState decode ───────────────────────────────────────────────────────

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
    ready: [0, 0, 0, 0],
    resLumber: [0, 0, 0, 0],
    resWool:   [0, 0, 0, 0],
    resGrain:  [0, 0, 0, 0],
    resBrick:  [0, 0, 0, 0],
    resOre:    [0, 0, 0, 0],
    vertices: Array.from({ length: 54 }, () => null),
    edges: Array.from({ length: 72 }, () => null),
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

/**
 * 54 vertices packed as 27 bytes (two 4-bit nibbles per byte, low nibble first).
 * Nibble value: 0x0..0x3 = settlement owner 0..3, 0x4..0x7 = city owner 0..3
 * (owner = nibble & 0x3), 0xF = empty. Last (55th) nibble is unused.
 */
function unpackVertices(packed: Uint8Array): (VertexOwner | null)[] {
  const out: (VertexOwner | null)[] = Array.from({ length: 54 }, () => null);
  for (let v = 0; v < 54; v++) {
    const byte = packed[v >> 1];
    if (byte === undefined) break;
    const nib = (v & 1) ? (byte >> 4) & 0x0f : byte & 0x0f;
    if (nib === 0x0f) continue;
    out[v] = { owner: nib & 0x03, city: (nib & 0x04) !== 0 };
  }
  return out;
}

/**
 * 72 edges packed as 36 bytes (two 4-bit nibbles per byte, low nibble first).
 * Nibble value: 0x0..0x3 = road owner, 0xF = empty.
 */
function unpackEdges(packed: Uint8Array): (EdgeOwner | null)[] {
  const out: (EdgeOwner | null)[] = Array.from({ length: 72 }, () => null);
  for (let e = 0; e < 72; e++) {
    const byte = packed[e >> 1];
    if (byte === undefined) break;
    const nib = (e & 1) ? (byte >> 4) & 0x0f : byte & 0x0f;
    if (nib === 0x0f) continue;
    out[e] = { owner: nib & 0x03 };
  }
  return out;
}

function readPackedVarints(chunk: Uint8Array): number[] {
  const out: number[] = [];
  let o = 0;
  while (o < chunk.length) {
    let v: number;
    [v, o] = readVarint(chunk, o);
    out.push(v);
  }
  return out;
}

function decodeBoardStatePayload(buf: Uint8Array): BoardState | null {
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
        case 13:
          state.tiles = unpackTiles(chunk);
          break;
        case 14: {
          const vps = readPackedVarints(chunk);
          state.vp = [vps[0] ?? 0, vps[1] ?? 0, vps[2] ?? 0, vps[3] ?? 0];
          break;
        }
        case 15: {
          const rs = readPackedVarints(chunk);
          state.ready = [rs[0] ?? 0, rs[1] ?? 0, rs[2] ?? 0, rs[3] ?? 0];
          break;
        }
        case 16:
          state.vertices = unpackVertices(chunk);
          break;
        case 17:
          state.edges = unpackEdges(chunk);
          break;
        case 20: {
          const vs = readPackedVarints(chunk);
          state.resLumber = [vs[0] ?? 0, vs[1] ?? 0, vs[2] ?? 0, vs[3] ?? 0];
          break;
        }
        case 21: {
          const vs = readPackedVarints(chunk);
          state.resWool = [vs[0] ?? 0, vs[1] ?? 0, vs[2] ?? 0, vs[3] ?? 0];
          break;
        }
        case 22: {
          const vs = readPackedVarints(chunk);
          state.resGrain = [vs[0] ?? 0, vs[1] ?? 0, vs[2] ?? 0, vs[3] ?? 0];
          break;
        }
        case 23: {
          const vs = readPackedVarints(chunk);
          state.resBrick = [vs[0] ?? 0, vs[1] ?? 0, vs[2] ?? 0, vs[3] ?? 0];
          break;
        }
        case 24: {
          const vs = readPackedVarints(chunk);
          state.resOre = [vs[0] ?? 0, vs[1] ?? 0, vs[2] ?? 0, vs[3] ?? 0];
          break;
        }
      }
    } else {
      return null;
    }
  }
  if (state.protoVersion !== CATAN_PROTO_VERSION) return null;
  return state;
}

export function decodeBoardState(base64: string): BoardState | null {
  const bytes = base64ToBytes(base64);
  if (bytes.length === 0) return null;
  return decodeBoardStatePayload(bytes);
}

// Back-compat alias — old name retained so callers don't break mid-migration.
export const decodeBoardStateFrame = decodeBoardState;
