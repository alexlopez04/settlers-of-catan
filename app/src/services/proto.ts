// =============================================================================
// proto.ts — Catan v8 wire protocol over BLE.
//
// The ESP32-C6 owns the entire game engine. The mobile is a pure display +
// input surface: it sends PlayerInput actions and renders BoardState. There is
// no ACTION_REPORT — VP and resources are computed by the board.
//
//   board -> mobile  (notify on State characteristic):    BoardState bytes
//   mobile -> board  (write on Input characteristic):     PlayerInput bytes
//   mobile -> board  (write once on Identity char):       client_id (UTF-8)
//   board -> mobile  (read+notify on Slot characteristic): assigned player_id
//
// react-native-ble-plx delivers/accepts characteristic values as base64.
// =============================================================================

// ── Wire constants (must match firmware/*/src/proto/catan.pb.h) ─────────────
export const CATAN_PROTO_VERSION = 8;

// ── Enums (mirror catan.proto) ──────────────────────────────────────────────

export enum GamePhase {
  LOBBY = 0,
  BOARD_SETUP = 1,
  NUMBER_REVEAL = 2,
  INITIAL_PLACEMENT = 3,
  PLAYING = 4,
  ROBBER = 5,
  DISCARD = 6,
  GAME_OVER = 7,
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

  BUY_ROAD = 10,
  BUY_SETTLEMENT = 11,
  BUY_CITY = 12,
  BUY_DEV_CARD = 13,

  PLACE_ROBBER = 14,
  STEAL_FROM = 15,
  DISCARD = 16,

  BANK_TRADE = 17,
  TRADE_OFFER = 18,
  TRADE_ACCEPT = 19,
  TRADE_DECLINE = 20,
  TRADE_CANCEL = 21,

  PLAY_KNIGHT = 22,
  PLAY_ROAD_BUILDING = 23,
  PLAY_YEAR_OF_PLENTY = 24,
  PLAY_MONOPOLY = 25,

  /** Lobby-only: Player 0 selects the board generation difficulty.
   *  Encode with `encodeSetDifficulty`. payload: monopolyRes = Difficulty value. */
  SET_DIFFICULTY = 26,

  /** Sent in response to the resume-game dialog (LOBBY phase only). */
  RESUME_YES = 27,
  RESUME_NO  = 28,
}

/** Board generation difficulty preset. Matches firmware Difficulty enum. */
export enum Difficulty {
  EASY   = 0,  // Balanced / Beginner Friendly
  NORMAL = 1,  // Classic Catan feel (default)
  HARD   = 2,  // Competitive / Skill-Focused
  EXPERT = 3,  // Punishing / Tournament Chaos
}

export const DIFFICULTY_LABEL: Record<Difficulty, string> = {
  [Difficulty.EASY]:   '🟢 Easy',
  [Difficulty.NORMAL]: '🟡 Normal',
  [Difficulty.HARD]:   '🟠 Hard',
  [Difficulty.EXPERT]: '🔴 Expert',
};

export const DIFFICULTY_DESCRIPTION: Record<Difficulty, string> = {
  [Difficulty.EASY]:   'Balanced & beginner friendly. No adjacent 6/8, fair resource spread.',
  [Difficulty.NORMAL]: 'Classic Catan feel with a near-standard random layout.',
  [Difficulty.HARD]:   'Competitive. Some scarcity, fewer prime intersections, tougher decisions.',
  [Difficulty.EXPERT]: 'Brutal. High-variance, heavy clustering, few optimal starting spots.',
};

export enum Biome {
  DESERT = 0,
  FOREST = 1,
  PASTURE = 2,
  FIELD = 3,
  HILL = 4,
  MOUNTAIN = 5,
}

/** Resource index — matches firmware Res enum. */
export enum Resource {
  LUMBER = 0,
  WOOL = 1,
  GRAIN = 2,
  BRICK = 3,
  ORE = 4,
}

/** Dev card index — matches firmware Dev enum. */
export enum DevCard {
  KNIGHT = 0,
  VP = 1,
  ROAD_BUILDING = 2,
  YEAR_OF_PLENTY = 3,
  MONOPOLY = 4,
}

export const RESOURCE_COUNT = 5;
export const DEV_CARD_COUNT = 5;
export const PLAYER_COUNT = 4;

// Mirrors firmware core::RejectReason.
export enum RejectReason {
  NONE                       = 0,
  OUT_OF_TURN                = 1,
  WRONG_PHASE                = 2,
  VERTEX_OCCUPIED            = 3,
  TOO_CLOSE_TO_SETTLEMENT    = 4,
  ROAD_OCCUPIED              = 5,
  ROAD_NOT_CONNECTED         = 6,
  NOT_MY_SETTLEMENT          = 7,
  ROBBER_SAME_TILE           = 8,
  INVALID_INDEX              = 9,
  NOT_PURCHASED              = 10,
  INSUFFICIENT_RESOURCES     = 11,
  PIECE_LIMIT_REACHED        = 12,
  BANK_DEPLETED              = 13,
  INVALID_TRADE              = 14,
  NO_PENDING_TRADE           = 15,
  DEV_CARD_NOT_AVAILABLE     = 16,
  DEV_DECK_EMPTY             = 17,
  INVALID_DISCARD            = 18,
  NOT_ELIGIBLE_TARGET        = 19,
  PLACEMENT_INCOMPLETE       = 20,
  SETUP_TURN_LIMIT           = 21,
}

export const REJECT_MESSAGES: Record<RejectReason, string> = {
  [RejectReason.NONE]:                     '',
  [RejectReason.OUT_OF_TURN]:              "It's not your turn",
  [RejectReason.WRONG_PHASE]:              'Wrong game phase for that action',
  [RejectReason.VERTEX_OCCUPIED]:          'That spot is already occupied',
  [RejectReason.TOO_CLOSE_TO_SETTLEMENT]:  'Too close to an existing settlement',
  [RejectReason.ROAD_OCCUPIED]:            'That road is already placed',
  [RejectReason.ROAD_NOT_CONNECTED]:       'Road must connect to your existing pieces',
  [RejectReason.NOT_MY_SETTLEMENT]:        "That's not your settlement",
  [RejectReason.ROBBER_SAME_TILE]:         'Move the robber to a different tile',
  [RejectReason.INVALID_INDEX]:            'Invalid board position',
  [RejectReason.NOT_PURCHASED]:            'Buy this piece from the store first',
  [RejectReason.INSUFFICIENT_RESOURCES]:   'Not enough resources for that purchase',
  [RejectReason.PIECE_LIMIT_REACHED]:      'No more of that piece left to place',
  [RejectReason.BANK_DEPLETED]:            'The bank does not have those resources',
  [RejectReason.INVALID_TRADE]:            'That trade is invalid',
  [RejectReason.NO_PENDING_TRADE]:         'There is no pending trade',
  [RejectReason.DEV_CARD_NOT_AVAILABLE]:   "You don't have that development card",
  [RejectReason.DEV_DECK_EMPTY]:           'The development card deck is empty',
  [RejectReason.INVALID_DISCARD]:          'Invalid discard amount',
  [RejectReason.NOT_ELIGIBLE_TARGET]:      'That player is not adjacent to the robber',
  [RejectReason.PLACEMENT_INCOMPLETE]:     'Place a settlement and a road before ending your turn',
  [RejectReason.SETUP_TURN_LIMIT]:         'You can only place one settlement and one road per setup turn',
};

// ── Public types ────────────────────────────────────────────────────────────

export interface Tile {
  biome: Biome;
  number: number;
}

/** Vertex ownership. */
export interface VertexOwner {
  /** Player 0..3 */
  owner: number;
  /** true = city, false = settlement */
  city: boolean;
}

export interface EdgeOwner {
  owner: number;
}

/** A pending bank/p2p trade offer. */
export interface PendingTrade {
  fromPlayer: number;       // 0xFF if no pending trade
  toPlayer: number;         // 0xFF = open offer
  offer: number[];          // length 5 (L,W,G,B,O)
  want: number[];           // length 5 (L,W,G,B,O)
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
  /** Length 4 — public VP (excludes hidden VP cards). */
  vp: number[];
  ready: number[];
  /** Length 4 each — current resource counts per player. */
  resLumber: number[];
  resWool: number[];
  resGrain: number[];
  resBrick: number[];
  resOre: number[];
  vertices: (VertexOwner | null)[];
  edges: (EdgeOwner | null)[];
  lastRejectReason: RejectReason;

  /** Length 20 — dev cards in player-major order (P0[K,VP,RB,YoP,Mono], P1..). */
  devCards: number[];
  /** Length 4 — knights played per player (visible). */
  knightsPlayed: number[];
  /** Player holding Largest Army (>=3 knights), or 0xFF. */
  largestArmyPlayer: number;
  /** Player holding Longest Road (>=5 roads), or 0xFF. */
  longestRoadPlayer: number;
  /** Length of holder's longest road chain, 0 if no holder. */
  longestRoadLength: number;
  /** Cards remaining in the dev deck. */
  devDeckRemaining: number;
  /** Whether the current player has played a non-VP dev card this turn. */
  cardPlayedThisTurn: boolean;

  /** Bitmask of players that must discard before robber can be moved. */
  discardRequiredMask: number;
  /** Length 4 — discard amount per player. */
  discardRequiredCount: number[];
  /** After PLACE_ROBBER, bitmask of players adjacent to the new tile with cards. */
  stealEligibleMask: number;

  /** Length 4 — pending pre-paid road purchases per player. */
  pendingRoadBuy: number[];
  /** Length 4 — pending pre-paid settlement purchases per player. */
  pendingSettlementBuy: number[];
  /** Length 4 — pending pre-paid city purchases per player. */
  pendingCityBuy: number[];
  /** Length 4 — free roads remaining from Road Building card. */
  freeRoadsRemaining: number[];

  /** Pending trade (single offer at a time). */
  trade: PendingTrade;

  /** Length 5 — bank supply (L,W,G,B,O). */
  bankSupply: number[];
  /** Length 20 — most recent dice-roll distribution, player-major. */
  lastDistribution: number[];

  /** Board generation difficulty (0=EASY, 1=NORMAL, 2=HARD, 3=EXPERT). */
  difficulty: Difficulty;

  /**
   * True when the board has a saved game from a previous power cycle that
   * the first connected player (slot 0) can choose to resume.
   * Cleared once the player responds with RESUME_YES or RESUME_NO.
   */
  hasSavedGame: boolean;
}

export interface PlayerInput {
  playerId: number;
  action: PlayerAction;
  clientId?: string;

  // Resource payloads — semantics depend on action (see catan.proto).
  resLumber?: number;
  resWool?: number;
  resGrain?: number;
  resBrick?: number;
  resOre?: number;
  wantLumber?: number;
  wantWool?: number;
  wantGrain?: number;
  wantBrick?: number;
  wantOre?: number;

  robberTile?: number;
  targetPlayer?: number;
  monopolyRes?: number;
  cardRes1?: number;
  cardRes2?: number;
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
//             res_*=11..15, want_*=16..20,
//             robber_tile=21, target_player=22, monopoly_res=23,
//             card_res_1=24, card_res_2=25.

function encodePlayerInputBody(input: PlayerInput): number[] {
  const out: number[] = [];
  encodeVarintField(1, CATAN_PROTO_VERSION, out);
  encodeVarintField(2, input.playerId | 0, out);
  encodeVarintField(3, input.action | 0, out);
  if (input.clientId)    encodeStringField(4, input.clientId, out);
  if (input.resLumber)   encodeVarintField(11, input.resLumber | 0, out);
  if (input.resWool)     encodeVarintField(12, input.resWool   | 0, out);
  if (input.resGrain)    encodeVarintField(13, input.resGrain  | 0, out);
  if (input.resBrick)    encodeVarintField(14, input.resBrick  | 0, out);
  if (input.resOre)      encodeVarintField(15, input.resOre    | 0, out);
  if (input.wantLumber)  encodeVarintField(16, input.wantLumber | 0, out);
  if (input.wantWool)    encodeVarintField(17, input.wantWool   | 0, out);
  if (input.wantGrain)   encodeVarintField(18, input.wantGrain  | 0, out);
  if (input.wantBrick)   encodeVarintField(19, input.wantBrick  | 0, out);
  if (input.wantOre)     encodeVarintField(20, input.wantOre    | 0, out);
  if (input.robberTile   !== undefined) encodeVarintField(21, input.robberTile   | 0, out);
  if (input.targetPlayer !== undefined) encodeVarintField(22, input.targetPlayer | 0, out);
  if (input.monopolyRes  !== undefined) encodeVarintField(23, input.monopolyRes  | 0, out);
  if (input.cardRes1     !== undefined) encodeVarintField(24, input.cardRes1     | 0, out);
  if (input.cardRes2     !== undefined) encodeVarintField(25, input.cardRes2     | 0, out);
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

// ── High-level action encoders ──────────────────────────────────────────────

export function encodeBuy(
  playerId: number,
  kind: 'road' | 'settlement' | 'city' | 'dev_card',
  clientId?: string,
): string {
  const action =
    kind === 'road'       ? PlayerAction.BUY_ROAD :
    kind === 'settlement' ? PlayerAction.BUY_SETTLEMENT :
    kind === 'city'       ? PlayerAction.BUY_CITY :
                            PlayerAction.BUY_DEV_CARD;
  return encodePlayerInput({ playerId, action, clientId });
}

export function encodePlaceRobber(
  playerId: number,
  tile: number,
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.PLACE_ROBBER,
    clientId,
    robberTile: tile,
  });
}

export function encodeStealFrom(
  playerId: number,
  targetPlayer: number,
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.STEAL_FROM,
    clientId,
    targetPlayer,
  });
}

/** counts: [lumber, wool, grain, brick, ore] */
export function encodeDiscard(
  playerId: number,
  counts: number[],
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.DISCARD,
    clientId,
    resLumber: counts[Resource.LUMBER] ?? 0,
    resWool:   counts[Resource.WOOL]   ?? 0,
    resGrain:  counts[Resource.GRAIN]  ?? 0,
    resBrick:  counts[Resource.BRICK]  ?? 0,
    resOre:    counts[Resource.ORE]    ?? 0,
  });
}

export function encodeBankTrade(
  playerId: number,
  give: number[],
  want: number[],
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.BANK_TRADE,
    clientId,
    resLumber: give[Resource.LUMBER] ?? 0,
    resWool:   give[Resource.WOOL]   ?? 0,
    resGrain:  give[Resource.GRAIN]  ?? 0,
    resBrick:  give[Resource.BRICK]  ?? 0,
    resOre:    give[Resource.ORE]    ?? 0,
    wantLumber: want[Resource.LUMBER] ?? 0,
    wantWool:   want[Resource.WOOL]   ?? 0,
    wantGrain:  want[Resource.GRAIN]  ?? 0,
    wantBrick:  want[Resource.BRICK]  ?? 0,
    wantOre:    want[Resource.ORE]    ?? 0,
  });
}

/** target=NO_PLAYER (0xFF) for an open offer to all opponents. */
export function encodeTradeOffer(
  playerId: number,
  target: number,
  offer: number[],
  want: number[],
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.TRADE_OFFER,
    clientId,
    targetPlayer: target,
    resLumber: offer[Resource.LUMBER] ?? 0,
    resWool:   offer[Resource.WOOL]   ?? 0,
    resGrain:  offer[Resource.GRAIN]  ?? 0,
    resBrick:  offer[Resource.BRICK]  ?? 0,
    resOre:    offer[Resource.ORE]    ?? 0,
    wantLumber: want[Resource.LUMBER] ?? 0,
    wantWool:   want[Resource.WOOL]   ?? 0,
    wantGrain:  want[Resource.GRAIN]  ?? 0,
    wantBrick:  want[Resource.BRICK]  ?? 0,
    wantOre:    want[Resource.ORE]    ?? 0,
  });
}

export function encodeTradeAccept(playerId: number, clientId?: string): string {
  return encodePlayerInput({ playerId, action: PlayerAction.TRADE_ACCEPT, clientId });
}
export function encodeTradeDecline(playerId: number, clientId?: string): string {
  return encodePlayerInput({ playerId, action: PlayerAction.TRADE_DECLINE, clientId });
}
export function encodeTradeCancel(playerId: number, clientId?: string): string {
  return encodePlayerInput({ playerId, action: PlayerAction.TRADE_CANCEL, clientId });
}

export function encodePlayKnight(playerId: number, clientId?: string): string {
  return encodePlayerInput({ playerId, action: PlayerAction.PLAY_KNIGHT, clientId });
}
export function encodePlayRoadBuilding(playerId: number, clientId?: string): string {
  return encodePlayerInput({ playerId, action: PlayerAction.PLAY_ROAD_BUILDING, clientId });
}
export function encodePlayYearOfPlenty(
  playerId: number,
  res1: Resource,
  res2: Resource,
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.PLAY_YEAR_OF_PLENTY,
    clientId,
    cardRes1: res1,
    cardRes2: res2,
  });
}
export function encodePlayMonopoly(
  playerId: number,
  res: Resource,
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.PLAY_MONOPOLY,
    clientId,
    monopolyRes: res,
  });
}

/**
 * Encode a SET_DIFFICULTY action.
 * Only Player 0 (player_id=0) should call this; the firmware silently ignores
 * attempts from other players.
 */
export function encodeSetDifficulty(
  playerId: number,
  difficulty: Difficulty,
  clientId?: string,
): string {
  return encodePlayerInput({
    playerId,
    action: PlayerAction.SET_DIFFICULTY,
    clientId,
    monopolyRes: difficulty,
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

const ZERO4 = (): number[] => [0, 0, 0, 0];
const ZERO5 = (): number[] => [0, 0, 0, 0, 0];
const ZERO20 = (): number[] => Array.from({ length: 20 }, () => 0);

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
    vp: ZERO4(),
    ready: ZERO4(),
    resLumber: ZERO4(),
    resWool:   ZERO4(),
    resGrain:  ZERO4(),
    resBrick:  ZERO4(),
    resOre:    ZERO4(),
    vertices: Array.from({ length: 54 }, () => null),
    edges: Array.from({ length: 72 }, () => null),
    lastRejectReason: RejectReason.NONE,
    devCards: ZERO20(),
    knightsPlayed: ZERO4(),
    largestArmyPlayer: NO_PLAYER,
    longestRoadPlayer: NO_PLAYER,
    longestRoadLength: 0,
    devDeckRemaining: 0,
    cardPlayedThisTurn: false,
    discardRequiredMask: 0,
    discardRequiredCount: ZERO4(),
    stealEligibleMask: 0,
    pendingRoadBuy: ZERO4(),
    pendingSettlementBuy: ZERO4(),
    pendingCityBuy: ZERO4(),
    freeRoadsRemaining: ZERO4(),
    trade: {
      fromPlayer: NO_PLAYER,
      toPlayer: NO_PLAYER,
      offer: ZERO5(),
      want: ZERO5(),
    },
    bankSupply: ZERO5(),
    lastDistribution: ZERO20(),
    difficulty: Difficulty.NORMAL,
    hasSavedGame: false,
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

function bytesToFixed(chunk: Uint8Array, len: number): number[] {
  const out: number[] = [];
  for (let i = 0; i < len; i++) out.push(chunk[i] ?? 0);
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
        case 25: state.lastRejectReason = v as RejectReason; break;
        case 32: state.largestArmyPlayer = v; break;
        case 33: state.longestRoadPlayer = v; break;
        case 34: state.longestRoadLength = v; break;
        case 35: state.devDeckRemaining = v; break;
        case 36: state.cardPlayedThisTurn = v !== 0; break;
        case 37: state.discardRequiredMask = v; break;
        case 39: state.stealEligibleMask = v; break;
        // trade_from_player / trade_to_player are 1-based on the wire
        // (0 = absent / open offer) to avoid proto3 zero-suppression of player 0.
        case 50: state.trade.fromPlayer = v === 0 ? NO_PLAYER : v - 1; break;
        case 51: state.trade.toPlayer   = v === 0 ? NO_PLAYER : v - 1; break;
        case 62: state.difficulty = v as Difficulty; break;
        case 63: state.hasSavedGame = v !== 0; break;
      }
    } else if (wireType === 2) {
      let len: number;
      [len, offset] = readVarint(buf, offset);
      const chunk = buf.slice(offset, offset + len);
      offset += len;
      switch (fieldNum) {
        case 13: state.tiles = unpackTiles(chunk); break;
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
        case 16: state.vertices = unpackVertices(chunk); break;
        case 17: state.edges = unpackEdges(chunk); break;
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
        case 30: state.devCards            = bytesToFixed(chunk, 20); break;
        case 31: state.knightsPlayed       = bytesToFixed(chunk, 4);  break;
        case 38: state.discardRequiredCount = bytesToFixed(chunk, 4); break;
        case 40: state.pendingRoadBuy       = bytesToFixed(chunk, 4); break;
        case 41: state.pendingSettlementBuy = bytesToFixed(chunk, 4); break;
        case 42: state.pendingCityBuy       = bytesToFixed(chunk, 4); break;
        case 43: state.freeRoadsRemaining   = bytesToFixed(chunk, 4); break;
        case 52: state.trade.offer          = bytesToFixed(chunk, 5); break;
        case 53: state.trade.want           = bytesToFixed(chunk, 5); break;
        case 60: state.bankSupply           = bytesToFixed(chunk, 5); break;
        case 61: state.lastDistribution     = bytesToFixed(chunk, 20); break;
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

// Back-compat alias.
export const decodeBoardStateFrame = decodeBoardState;

// ── Helpers for the UI ──────────────────────────────────────────────────────

/** Read a player's dev card count from the BoardState packed array. */
export function devCardCount(state: BoardState, player: number, card: DevCard): number {
  return state.devCards[player * DEV_CARD_COUNT + card] ?? 0;
}

/** Read a single (player, resource) byte from the lastDistribution array. */
export function lastDistributionFor(state: BoardState, player: number, res: Resource): number {
  return state.lastDistribution[player * RESOURCE_COUNT + res] ?? 0;
}

/** Get a player's resource counts as a length-5 array (L,W,G,B,O). */
export function playerResources(state: BoardState, player: number): number[] {
  return [
    state.resLumber[player] ?? 0,
    state.resWool[player]   ?? 0,
    state.resGrain[player]  ?? 0,
    state.resBrick[player]  ?? 0,
    state.resOre[player]    ?? 0,
  ];
}

/** Total resource cards held by a player. */
export function playerTotalCards(state: BoardState, player: number): number {
  return playerResources(state, player).reduce((a, b) => a + b, 0);
}
