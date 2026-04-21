// ── Enums (mirrors catan.proto) ───────────────────────────────────────────

export enum MsgType {
  MSG_NONE = 0,
  MSG_GAME_STATE = 1,
  MSG_DICE_RESULT = 2,
  MSG_PROMPT = 3,
  MSG_BUTTON_EVENT = 10,
  MSG_PLAYER_READY = 11,
  MSG_ACTION = 12,
}

export enum GamePhase {
  PHASE_WAITING_FOR_PLAYERS = 0,
  PHASE_BOARD_SETUP = 1,
  PHASE_NUMBER_REVEAL = 2,
  PHASE_INITIAL_PLACEMENT = 3,
  PHASE_PLAYING = 4,
  PHASE_ROBBER = 5,
  PHASE_TRADE = 6,
  PHASE_GAME_OVER = 7,
}

export enum PlayerAction {
  ACTION_NONE = 0,
  ACTION_BTN_LEFT = 1,
  ACTION_BTN_CENTER = 2,
  ACTION_BTN_RIGHT = 3,
  ACTION_ROLL_DICE = 4,
  ACTION_END_TURN = 5,
  ACTION_TRADE = 6,
  ACTION_SKIP_ROBBER = 7,
  ACTION_PLACE_DONE = 8,
  ACTION_START_GAME = 9,
  ACTION_NEXT_NUMBER = 10,
}

// ── Message types ─────────────────────────────────────────────────────────

export interface BoardToPlayer {
  protoVersion: number;
  type: MsgType;
  phase: GamePhase;
  currentPlayer: number;
  numPlayers: number;
  yourPlayerId: number;
  die1: number;
  die2: number;
  diceTotal: number;
  revealNumber: number;
  line1: string;
  line2: string;
  btnLeft: string;
  btnCenter: string;
  btnRight: string;
  vp: [number, number, number, number];
  resLumber: number;
  resWool: number;
  resGrain: number;
  resBrick: number;
  resOre: number;
  winnerId: number;
  setupRound: number;
  hasRolled: boolean;
}

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

function writeVarint(value: number): number[] {
  const bytes: number[] = [];
  let v = value >>> 0;
  while (v > 0x7f) {
    bytes.push((v & 0x7f) | 0x80);
    v >>>= 7;
  }
  bytes.push(v & 0x7f);
  return bytes;
}

/** Encodes a varint field only when value is non-zero (proto3 default omission). */
function encodeVarintField(fieldNum: number, value: number): number[] {
  if (!value) return [];
  return [...writeVarint((fieldNum << 3) | 0), ...writeVarint(value)];
}

/** Decode plain ASCII / Latin-1 bytes to string (BLE strings are ASCII, max 22 chars). */
function decodeBytes(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map(b => String.fromCharCode(b))
    .join('');
}

// ── Base64 ↔ Uint8Array ───────────────────────────────────────────────────

function base64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

function bytesToBase64(bytes: Uint8Array): string {
  let bin = '';
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

// ── Encode PlayerToBoard ──────────────────────────────────────────────────

/** Returns a base64-encoded PlayerToBoard{type=MSG_ACTION, action} message. */
export function encodeAction(action: PlayerAction): string {
  const bytes = new Uint8Array([
    ...encodeVarintField(1, MsgType.MSG_ACTION),
    ...encodeVarintField(3, action),
  ]);
  return bytesToBase64(bytes);
}

// ── Decode BoardToPlayer ──────────────────────────────────────────────────

const NO_WINNER = 0xff;

/** Parses a base64 NanoPB-encoded BoardToPlayer message. */
export function decodeBoardToPlayer(base64: string): BoardToPlayer {
  const buf = base64ToBytes(base64);

  const state: BoardToPlayer = {
    protoVersion: 0,
    type: MsgType.MSG_NONE,
    phase: GamePhase.PHASE_WAITING_FOR_PLAYERS,
    currentPlayer: 0,
    numPlayers: 0,
    yourPlayerId: 0,
    die1: 0,
    die2: 0,
    diceTotal: 0,
    revealNumber: 0,
    line1: '',
    line2: '',
    btnLeft: '',
    btnCenter: '',
    btnRight: '',
    vp: [0, 0, 0, 0],
    resLumber: 0,
    resWool: 0,
    resGrain: 0,
    resBrick: 0,
    resOre: 0,
    winnerId: NO_WINNER,
    setupRound: 0,
    hasRolled: false,
  };

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
        case 30: state.protoVersion = v; break;
        case 1:  state.type = v; break;
        case 2:  state.phase = v; break;
        case 3:  state.currentPlayer = v; break;
        case 4:  state.numPlayers = v; break;
        case 5:  state.yourPlayerId = v; break;
        case 6:  state.die1 = v; break;
        case 7:  state.die2 = v; break;
        case 8:  state.diceTotal = v; break;
        case 9:  state.revealNumber = v; break;
        case 15: state.vp[0] = v; break;
        case 16: state.vp[1] = v; break;
        case 17: state.vp[2] = v; break;
        case 18: state.vp[3] = v; break;
        case 19: state.resLumber = v; break;
        case 20: state.resWool = v; break;
        case 21: state.resGrain = v; break;
        case 22: state.resBrick = v; break;
        case 23: state.resOre = v; break;
        case 24: state.winnerId = v; break;
        case 25: state.setupRound = v; break;
        case 26: state.hasRolled = v !== 0; break;
      }
    } else if (wireType === 2) {
      let len: number;
      [len, offset] = readVarint(buf, offset);
      const s = decodeBytes(buf.slice(offset, offset + len));
      offset += len;
      switch (fieldNum) {
        case 10: state.line1 = s; break;
        case 11: state.line2 = s; break;
        case 12: state.btnLeft = s; break;
        case 13: state.btnCenter = s; break;
        case 14: state.btnRight = s; break;
      }
    } else {
      // Unknown wire type — stop parsing to avoid corruption
      break;
    }
  }

  return state;
}

export { NO_WINNER };
