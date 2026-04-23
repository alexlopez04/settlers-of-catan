// =============================================================================
// proto.ts — Catan v4 wire protocol: Envelope { BoardState | PlayerInput | … }
//
// Every message on every hop (Serial, LoRa, BLE) is a framed Envelope:
//
//   [ 0xCA magic ] [ len : uint8 ] [ nanopb Envelope payload : len bytes ]
//
// The mobile app originates PlayerInput envelopes with sender_id=MOBILE_N and
// its own monotonic sequence counter; it receives BoardState envelopes from
// the player station it's connected to.
//
// BLE characteristic values from react-native-ble-plx are base64-encoded.
// =============================================================================

// ── Wire constants (must match firmware/{*}/src/catan_wire.h) ────────────
export const CATAN_WIRE_MAGIC = 0xca;
export const CATAN_FRAME_HEADER = 2;
export const CATAN_PROTO_VERSION = 4;

// Node id constants — see catan_wire.h / CATAN_NODE_*.
export const CATAN_NODE_BOARD = 1;
export const CATAN_NODE_BRIDGE = 2;
export const CATAN_NODE_PLAYER_BASE = 10;
export const CATAN_NODE_MOBILE_BASE = 20;

// Envelope.body oneof field tags (mirror catan.proto).
const ENV_TAG_BOARD_STATE = 10;
const ENV_TAG_PLAYER_INPUT = 11;
const ENV_TAG_ACK = 12;
// const ENV_TAG_NACK = 13;
// const ENV_TAG_SYNC_REQUEST = 14;

// Envelope header field tags.
const ENV_TAG_PROTO_VERSION = 1;
const ENV_TAG_SENDER_ID = 2;
const ENV_TAG_SEQ = 3;
const ENV_TAG_TIMESTAMP = 4;
const ENV_TAG_MSG_TYPE = 5;
const ENV_TAG_RELIABLE = 6;

// MessageType enum (matches Envelope.body oneof tag numbers).
export enum MessageType {
  UNSPECIFIED = 0,
  BOARD_STATE = ENV_TAG_BOARD_STATE,
  PLAYER_INPUT = ENV_TAG_PLAYER_INPUT,
  ACK = ENV_TAG_ACK,
  NACK = 13,
  SYNC_REQUEST = 14,
}

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
  REQUEST_SYNC = 9,
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

/** Decoded BoardState. `protoVersion` is surfaced from the envelope header. */
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
  /** Envelope metadata from the last received frame (read-only). */
  senderId?: number;
  sequenceNumber?: number;
}

export interface PlayerInput {
  /** Kept for backwards compatibility with callers; no longer on the wire. */
  protoVersion?: number;
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

function encodeBoolField(fieldNum: number, value: boolean, out: number[]): void {
  if (!value) return;
  writeVarint((fieldNum << 3) | 0, out);
  writeVarint(1, out);
}

/** Writes a length-delimited sub-message field: [tag][len][bytes]. */
function encodeSubmessage(fieldNum: number, payload: number[], out: number[]): void {
  writeVarint((fieldNum << 3) | 2, out);
  writeVarint(payload.length, out);
  for (const b of payload) out.push(b);
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

function frameBase64(payload: number[]): string {
  if (payload.length > 0xff) throw new Error(`payload too big: ${payload.length}`);
  const framed = [CATAN_WIRE_MAGIC, payload.length & 0xff, ...payload];
  return bytesToBase64(framed);
}

function unframe(bytes: Uint8Array): Uint8Array | null {
  if (bytes.length < CATAN_FRAME_HEADER) return null;
  if (bytes[0] !== CATAN_WIRE_MAGIC) return null;
  const len = bytes[1];
  if (len === 0 || CATAN_FRAME_HEADER + len > bytes.length) return null;
  return bytes.slice(CATAN_FRAME_HEADER, CATAN_FRAME_HEADER + len);
}

// ── Outbound sequence state ───────────────────────────────────────────────

let mobileTxSeq = 0;
function nextMobileSeq(): number {
  mobileTxSeq = (mobileTxSeq + 1) >>> 0;
  if (mobileTxSeq === 0) mobileTxSeq = 1;
  return mobileTxSeq;
}

/** Mobile's sender_id is derived from the player slot it's connected to. */
function mobileSenderId(playerId: number): number {
  return CATAN_NODE_MOBILE_BASE + (playerId & 0x03);
}

// ── PlayerInput body encoder (inner message only, no frame, no envelope) ──
// Tags (v4): player_id=1, action=2, vp=10, res_lumber=11, res_wool=12,
// res_grain=13, res_brick=14, res_ore=15.
function encodePlayerInputBody(input: PlayerInput): number[] {
  const out: number[] = [];
  encodeVarintField(1, input.playerId | 0, out);
  encodeVarintField(2, input.action | 0, out);
  if (input.vp)        encodeVarintField(10, input.vp | 0, out);
  if (input.resLumber) encodeVarintField(11, input.resLumber | 0, out);
  if (input.resWool)   encodeVarintField(12, input.resWool | 0, out);
  if (input.resGrain)  encodeVarintField(13, input.resGrain | 0, out);
  if (input.resBrick)  encodeVarintField(14, input.resBrick | 0, out);
  if (input.resOre)    encodeVarintField(15, input.resOre | 0, out);
  return out;
}

// ── Envelope encoder ──────────────────────────────────────────────────────
// Writes the envelope header fields, then the single body sub-message.
interface EnvelopeMeta {
  senderId: number;
  reliable: boolean;
  /** Overrides the module-scope sequence counter. Optional. */
  sequenceNumber?: number;
}

function encodeEnvelope(
  meta: EnvelopeMeta,
  bodyTag: number,
  bodyBytes: number[],
): number[] {
  const out: number[] = [];
  const seq = meta.sequenceNumber ?? nextMobileSeq();
  encodeVarintField(ENV_TAG_PROTO_VERSION, CATAN_PROTO_VERSION, out);
  encodeVarintField(ENV_TAG_SENDER_ID, meta.senderId, out);
  encodeVarintField(ENV_TAG_SEQ, seq, out);
  encodeVarintField(ENV_TAG_TIMESTAMP, Date.now() & 0xffffffff, out);
  encodeVarintField(ENV_TAG_MSG_TYPE, bodyTag, out);
  encodeBoolField(ENV_TAG_RELIABLE, meta.reliable, out);
  encodeSubmessage(bodyTag, bodyBytes, out);
  return out;
}

// ── Public encoders ───────────────────────────────────────────────────────

/** Encodes + frames a PlayerInput envelope, returning a base64 BLE payload.
 *  The station will force the `playerId` to match its hardware, but we set
 *  the requested value anyway for log/debug clarity. */
export function encodePlayerInput(input: PlayerInput): string {
  const body = encodePlayerInputBody(input);
  const reliable =
    input.action !== PlayerAction.NONE &&
    input.action !== PlayerAction.READY &&
    input.action !== PlayerAction.REPORT;
  const env = encodeEnvelope(
    { senderId: mobileSenderId(input.playerId), reliable },
    ENV_TAG_PLAYER_INPUT,
    body,
  );
  return frameBase64(env);
}

/** Convenience helper for simple action-only inputs. */
export function encodeAction(playerId: number, action: PlayerAction): string {
  return encodePlayerInput({ playerId, action });
}

/** Self-report current VP + resource counts (always unreliable — reports
 *  are high-frequency and tolerant of loss). */
export function encodeReport(
  playerId: number,
  vp: number,
  resources: { lumber: number; wool: number; grain: number; brick: number; ore: number },
): string {
  return encodePlayerInput({
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

// ── Decode ────────────────────────────────────────────────────────────────

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

/** Decodes a BoardState sub-message payload (Envelope.body.board_state). */
function decodeBoardStateBody(buf: Uint8Array, state: BoardState): void {
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
          // Packed repeated uint32.
          const vps: number[] = [];
          let o = 0;
          while (o < chunk.length) {
            let v: number;
            [v, o] = readVarint(chunk, o);
            vps.push(v);
          }
          state.vp = [vps[0] ?? 0, vps[1] ?? 0, vps[2] ?? 0, vps[3] ?? 0];
          break;
        }
      }
    } else {
      break;
    }
  }
}

/** Decodes an Envelope payload (no frame), returning the BoardState if the
 *  envelope carries one, else null. */
export function decodeBoardStatePayload(buf: Uint8Array): BoardState | null {
  const state = emptyBoardState();
  let sawBoardState = false;
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
        case ENV_TAG_PROTO_VERSION:
          state.protoVersion = v;
          break;
        case ENV_TAG_SENDER_ID:
          state.senderId = v;
          break;
        case ENV_TAG_SEQ:
          state.sequenceNumber = v;
          break;
        case ENV_TAG_TIMESTAMP:
        case ENV_TAG_MSG_TYPE:
        case ENV_TAG_RELIABLE:
          // Consumed but not surfaced in BoardState.
          break;
      }
    } else if (wireType === 2) {
      let len: number;
      [len, offset] = readVarint(buf, offset);
      const chunk = buf.slice(offset, offset + len);
      offset += len;
      if (fieldNum === ENV_TAG_BOARD_STATE) {
        decodeBoardStateBody(chunk, state);
        sawBoardState = true;
      }
      // Other body tags (ack/nack/sync_request) are ignored in Phase 1.
    } else {
      break;
    }
  }
  if (!sawBoardState) return null;
  if (state.protoVersion !== CATAN_PROTO_VERSION) return null;
  return state;
}

/** Parses a base64 Catan frame containing an Envelope. Returns the inner
 *  BoardState, or null if the envelope was malformed, wrong version, or
 *  carried a non-BoardState body. */
export function decodeBoardStateFrame(base64: string): BoardState | null {
  const bytes = base64ToBytes(base64);
  const payload = unframe(bytes);
  if (!payload) return null;
  return decodeBoardStatePayload(payload);
}

// Back-compat alias.
export const decodeBoardState = decodeBoardStateFrame;
