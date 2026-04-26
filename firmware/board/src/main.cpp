// =============================================================================
// main.cpp — Settlers of Catan: unified ESP32-C6 board controller.
//
//   ESP32-C6  <--I²C(400k)-->  PCF8575 expanders   (Hall sensor input)
//   ESP32-C6  <-- BLE -->      up to 4 mobile phones (Identity / Slot / State / Input)
//   ESP32-C6  --> RMT / GPIO10 --> WS2812B strip (FastLED)
//
// One MCU owns: game FSM, sensor scanning, LED rendering, BLE peripheral.
// The mobile is a thin client — see proto/catan.proto v8.
//
// Threading: NimBLE owns its own host task (created by the library). All
// game logic runs in the Arduino loop() task. BLE→game data crosses the
// task boundary via a FreeRTOS queue inside comms.cpp; game→BLE notifies
// run on the loop() task and are serialised by the NimBLE stack.
// =============================================================================

#include <Arduino.h>
#include <esp_system.h>
#include <esp_random.h>

#include "config.h"
#include "catan_log.h"
#include "catan_wire.h"
#include "comms.h"
#include "board_types.h"
#include "board_topology.h"
#include "led_manager.h"
#include "sensor_manager.h"
#include "game_state.h"
#include "dice.h"
#include "proto/catan.pb.h"
#include "core/state_machine.h"
#include "core/events.h"
#include "core/rng.h"

static core::StateMachine sm;

// ── Timing ──────────────────────────────────────────────────────────────────
static uint32_t last_broadcast_ms = 0;
static uint32_t last_heartbeat_ms = 0;
static uint32_t last_demo_ms      = 0;
static uint32_t loop_count        = 0;

// ── Player colours ──────────────────────────────────────────────────────────
static const CRGB kPlayerColors[MAX_PLAYERS] = {
    CRGB::Red, CRGB::Blue, CRGB::Orange, CRGB::White
};

// ── Lobby orientation-pattern constants ─────────────────────────────────────
// Three tiles form a rotationally-unique triangle: T14 (red), T18 (green),
// T10 (blue).  The mobile app mirrors these three points so players can
// calibrate their perspective before the game.
struct OrientationPoint { uint8_t tile; CRGB color; };
static const OrientationPoint kOrientationPoints[] = {
    { 14, CRGB(220,   0,   0) },  // red
    { 18, CRGB(  0, 200,   0) },  // green
    { 10, CRGB(  0,   0, 220) },  // blue
};

// Render the lobby LED state: dim base + orientation triangle + player slots.
static void showLobbyLeds() {
    const uint8_t mask = comms::connectedMask();
    led::setAllTiles(CRGB(20, 20, 40));  // dim blue-grey base

    // Orientation triangle
    for (const auto& p : kOrientationPoints)
        led::setTileColor(p.tile, p.color);

    // Connected-player indicators on tiles 0–3
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (mask & (1 << i)) led::setTileColor(i, kPlayerColors[i]);
    }
    led::show();
}

static const CRGB kDemoColors[] = {
    CRGB(0,   200,   0), CRGB(255, 255, 0), CRGB(255, 165, 0),
    CRGB(255, 0,     0), CRGB(128, 0, 128), CRGB(255, 255, 255),
};
static constexpr uint8_t kDemoColorCount =
    (uint8_t)(sizeof(kDemoColors) / sizeof(kDemoColors[0]));

static void runDemoFrame() {
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        led::setTileColor(t, kDemoColors[core::rng::uniform(kDemoColorCount)]);
    }
    led::show();
}

static CRGB portColor(PortType pt) {
    switch (pt) {
        case PortType::LUMBER_2_1:  return CRGB(0, 200, 0);
        case PortType::WOOL_2_1:    return CRGB(255, 255, 0);
        case PortType::GRAIN_2_1:   return CRGB(255, 165, 0);
        case PortType::BRICK_2_1:   return CRGB(255, 0, 0);
        case PortType::ORE_2_1:     return CRGB(128, 0, 128);
        case PortType::GENERIC_3_1: return CRGB::White;
        default:                    return CRGB::Black;
    }
}

static uint8_t biomeCode(Biome b) {
    switch (b) {
        case Biome::FOREST:   return 1;
        case Biome::PASTURE:  return 2;
        case Biome::FIELD:    return 3;
        case Biome::HILL:     return 4;
        case Biome::MOUNTAIN: return 5;
        default:              return 0;
    }
}

static catan_GamePhase phaseToProto(GamePhase p) {
    switch (p) {
        case GamePhase::LOBBY:             return catan_GamePhase_PHASE_LOBBY;
        case GamePhase::BOARD_SETUP:       return catan_GamePhase_PHASE_BOARD_SETUP;
        case GamePhase::NUMBER_REVEAL:     return catan_GamePhase_PHASE_NUMBER_REVEAL;
        case GamePhase::INITIAL_PLACEMENT: return catan_GamePhase_PHASE_INITIAL_PLACEMENT;
        case GamePhase::PLAYING:           return catan_GamePhase_PHASE_PLAYING;
        case GamePhase::ROBBER:            return catan_GamePhase_PHASE_ROBBER;
        case GamePhase::DISCARD:           return catan_GamePhase_PHASE_DISCARD;
        case GamePhase::GAME_OVER:         return catan_GamePhase_PHASE_GAME_OVER;
    }
    return catan_GamePhase_PHASE_LOBBY;
}

static core::ActionKind toActionKind(catan_PlayerAction a) {
    switch (a) {
        case catan_PlayerAction_ACTION_READY:               return core::ActionKind::READY;
        case catan_PlayerAction_ACTION_START_GAME:          return core::ActionKind::START_GAME;
        case catan_PlayerAction_ACTION_NEXT_NUMBER:         return core::ActionKind::NEXT_NUMBER;
        case catan_PlayerAction_ACTION_PLACE_DONE:          return core::ActionKind::PLACE_DONE;
        case catan_PlayerAction_ACTION_ROLL_DICE:           return core::ActionKind::ROLL_DICE;
        case catan_PlayerAction_ACTION_END_TURN:            return core::ActionKind::END_TURN;
        case catan_PlayerAction_ACTION_SKIP_ROBBER:         return core::ActionKind::SKIP_ROBBER;
        case catan_PlayerAction_ACTION_BUY_ROAD:            return core::ActionKind::BUY_ROAD;
        case catan_PlayerAction_ACTION_BUY_SETTLEMENT:      return core::ActionKind::BUY_SETTLEMENT;
        case catan_PlayerAction_ACTION_BUY_CITY:            return core::ActionKind::BUY_CITY;
        case catan_PlayerAction_ACTION_BUY_DEV_CARD:        return core::ActionKind::BUY_DEV_CARD;
        case catan_PlayerAction_ACTION_PLACE_ROBBER:        return core::ActionKind::PLACE_ROBBER;
        case catan_PlayerAction_ACTION_STEAL_FROM:          return core::ActionKind::STEAL_FROM;
        case catan_PlayerAction_ACTION_DISCARD:             return core::ActionKind::DISCARD;
        case catan_PlayerAction_ACTION_BANK_TRADE:          return core::ActionKind::BANK_TRADE;
        case catan_PlayerAction_ACTION_TRADE_OFFER:         return core::ActionKind::TRADE_OFFER;
        case catan_PlayerAction_ACTION_TRADE_ACCEPT:        return core::ActionKind::TRADE_ACCEPT;
        case catan_PlayerAction_ACTION_TRADE_DECLINE:       return core::ActionKind::TRADE_DECLINE;
        case catan_PlayerAction_ACTION_TRADE_CANCEL:        return core::ActionKind::TRADE_CANCEL;
        case catan_PlayerAction_ACTION_PLAY_KNIGHT:         return core::ActionKind::PLAY_KNIGHT;
        case catan_PlayerAction_ACTION_PLAY_ROAD_BUILDING:  return core::ActionKind::PLAY_ROAD_BUILDING;
        case catan_PlayerAction_ACTION_PLAY_YEAR_OF_PLENTY: return core::ActionKind::PLAY_YEAR_OF_PLENTY;
        case catan_PlayerAction_ACTION_PLAY_MONOPOLY:       return core::ActionKind::PLAY_MONOPOLY;
        default:                                            return core::ActionKind::NONE;
    }
}

static const char* rejectReasonName(core::RejectReason r) {
    switch (r) {
        case core::RejectReason::OUT_OF_TURN:              return "OUT_OF_TURN";
        case core::RejectReason::WRONG_PHASE:              return "WRONG_PHASE";
        case core::RejectReason::VERTEX_OCCUPIED:          return "VERTEX_OCCUPIED";
        case core::RejectReason::TOO_CLOSE_TO_SETTLEMENT:  return "TOO_CLOSE";
        case core::RejectReason::ROAD_OCCUPIED:            return "ROAD_OCCUPIED";
        case core::RejectReason::ROAD_NOT_CONNECTED:       return "NOT_CONNECTED";
        case core::RejectReason::NOT_MY_SETTLEMENT:        return "NOT_MY_SETTLEMENT";
        case core::RejectReason::ROBBER_SAME_TILE:         return "ROBBER_SAME";
        case core::RejectReason::INVALID_INDEX:            return "INVALID_INDEX";
        case core::RejectReason::NOT_PURCHASED:            return "NOT_PURCHASED";
        case core::RejectReason::INSUFFICIENT_RESOURCES:   return "NO_RES";
        case core::RejectReason::PIECE_LIMIT_REACHED:      return "PIECE_LIMIT";
        case core::RejectReason::BANK_DEPLETED:            return "BANK_DEPLETED";
        case core::RejectReason::INVALID_TRADE:            return "INVALID_TRADE";
        case core::RejectReason::NO_PENDING_TRADE:         return "NO_TRADE";
        case core::RejectReason::DEV_CARD_NOT_AVAILABLE:   return "DEV_NA";
        case core::RejectReason::DEV_DECK_EMPTY:           return "DECK_EMPTY";
        case core::RejectReason::INVALID_DISCARD:          return "BAD_DISCARD";
        case core::RejectReason::NOT_ELIGIBLE_TARGET:      return "BAD_TARGET";
        default:                                           return "NONE";
    }
}

// Forward declarations.
static void broadcastBoardState();

// ---------------------------------------------------------------------------
// Effect → hardware side-effects
// ---------------------------------------------------------------------------
static void applyEffect(const core::Effect& ef) {
    using core::EffectKind;
    switch (ef.kind) {
        case EffectKind::PHASE_ENTERED: {
            GamePhase p = (GamePhase)ef.a;
            LOGI("FSM", "phase -> %s", game::phaseName(p));
            if (p == GamePhase::LOBBY) {
                showLobbyLeds();
            } else if (p == GamePhase::PLAYING) {
                led::colorAllTilesByBiome();
                if (game::robberTile() < TILE_COUNT) led::dimTile(game::robberTile());
                led::show();
            }
            break;
        }
        case EffectKind::LOBBY_MASK_CHANGED: {
            uint8_t mask = ef.a;
            LOGI("LOBBY", "connected mask=0x%02X (%u players, ready=0x%02X)",
                 (unsigned)mask, (unsigned)game::numPlayers(),
                 (unsigned)game::readyMask());
            if (game::phase() == GamePhase::LOBBY) {
                static uint8_t s_prevMask = 0;
                uint8_t added = mask & ~s_prevMask;
                s_prevMask = mask;
                // Set lobby base colors first so the flash restores correctly.
                showLobbyLeds();
                if (added) {
                    // Find the lowest newly-connected player and flash all tiles.
                    static uint8_t s_allTiles[TILE_COUNT];
                    for (uint8_t i = 0; i < TILE_COUNT; ++i) s_allTiles[i] = i;
                    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                        if (added & (1u << i)) {
                            led::flashTiles(s_allTiles, TILE_COUNT,
                                            kPlayerColors[i], 3, 100);
                            break;
                        }
                    }
                }
            }
            break;
        }
        case EffectKind::BOARD_RANDOMIZED: {
            LOGI("SETUP", "board randomized");
            led::colorAllTilesByBiome();
            for (uint8_t p = 0; p < PORT_COUNT; ++p) {
                led::setPortColor(p, portColor(PORT_TOPO[p].type));
            }
            led::show();
            for (uint8_t t = 0; t < TILE_COUNT; ++t) {
                LOGI("TILE", "%2u %-8s #%u", (unsigned)t,
                     biomeName(g_tile_state[t].biome),
                     (unsigned)g_tile_state[t].number);
            }
            break;
        }
        case EffectKind::REVEAL_NUMBER_CHANGED: {
            uint8_t num = ef.a;
            if (num == 0) {
                led::colorAllTilesByBiome();
                if (game::robberTile() < TILE_COUNT) led::dimTile(game::robberTile());
            } else {
                led::setAllTiles(CRGB::Black);
                for (uint8_t t = 0; t < TILE_COUNT; ++t) {
                    if (g_tile_state[t].number == num) led::setTileColor(t, CRGB::White);
                }
                LOGI("REVEAL", "number=%u", (unsigned)num);
            }
            led::show();
            break;
        }
        case EffectKind::PLACED_SETTLEMENT: {
            uint8_t p = ef.a, v = ef.b;
            LOGI("PLACE", "P%u settlement v%u", (unsigned)(p + 1), (unsigned)v);
            uint8_t adj[3];
            uint8_t cnt = tilesForVertex(v, adj, 3);
            if (cnt > 0) led::flashTiles(adj, cnt, kPlayerColors[p % MAX_PLAYERS], 2, 150);
            break;
        }
        case EffectKind::PLACED_CITY: {
            uint8_t p = ef.a, v = ef.b;
            LOGI("PLACE", "P%u city v%u", (unsigned)(p + 1), (unsigned)v);
            uint8_t adj[3];
            uint8_t cnt = tilesForVertex(v, adj, 3);
            if (cnt > 0) led::flashTiles(adj, cnt, kPlayerColors[p % MAX_PLAYERS], 3, 150);
            break;
        }
        case EffectKind::PLACED_ROAD:
            LOGI("PLACE", "P%u road e%u", (unsigned)(ef.a + 1), (unsigned)ef.b);
            break;
        case EffectKind::PLACEMENT_REJECTED:
            LOGW("REJECT", "P%u reason=%s", (unsigned)(ef.a + 1),
                 rejectReasonName((core::RejectReason)ef.b));
            game::setLastRejectReason(ef.b);
            break;
        case EffectKind::DICE_ROLLED: {
            uint8_t d1 = ef.a, d2 = ef.b;
            uint8_t total = (uint8_t)(d1 + d2);
            LOGI("DICE", "P%u rolled %u+%u=%u%s",
                 (unsigned)(game::currentPlayer() + 1),
                 (unsigned)d1, (unsigned)d2, (unsigned)total,
                 ef.c ? " -> ROBBER" : "");
            broadcastBoardState();
            last_broadcast_ms = millis();
            uint8_t matching[TILE_COUNT];
            uint8_t match_count = 0;
            for (uint8_t t = 0; t < TILE_COUNT; ++t) {
                if (g_tile_state[t].number == total && t != game::robberTile()) {
                    matching[match_count++] = t;
                }
            }
            if (match_count > 0)
                led::flashTiles(matching, match_count, CRGB::White, 3, 200);
            break;
        }
        case EffectKind::TURN_ADVANCED:
            LOGI("TURN", "-> P%u", (unsigned)(ef.a + 1));
            break;
        case EffectKind::ROBBER_MOVED: {
            uint8_t new_t = ef.a, old_t = ef.b;
            if (old_t < TILE_COUNT) led::undimTile(old_t);
            if (new_t < TILE_COUNT) led::dimTile(new_t);
            led::show();
            LOGI("ROBBER", "tile %u -> %u", (unsigned)old_t, (unsigned)new_t);
            break;
        }
        case EffectKind::RESOURCES_DISTRIBUTED:
            LOGI("DIST", "roll=%u dealt=%u", (unsigned)ef.a, (unsigned)ef.b);
            break;
        case EffectKind::DISCARD_REQUIRED:
            LOGI("DISC", "mask=0x%02X", (unsigned)ef.a);
            break;
        case EffectKind::DISCARD_COMPLETED:
            LOGI("DISC", "P%u discarded %u", (unsigned)(ef.a + 1), (unsigned)ef.b);
            break;
        case EffectKind::STEAL_OCCURRED:
            LOGI("STEAL", "P%u stole res=%u from P%u",
                 (unsigned)(ef.a + 1), (unsigned)ef.c, (unsigned)(ef.b + 1));
            break;
        case EffectKind::DEV_CARD_DRAWN:
            LOGI("DEV", "P%u drew card type=%u", (unsigned)(ef.a + 1), (unsigned)ef.b);
            break;
        case EffectKind::KNIGHT_PLAYED:
            LOGI("DEV", "P%u played KNIGHT", (unsigned)(ef.a + 1));
            break;
        case EffectKind::ROAD_BUILDING_PLAYED:
            LOGI("DEV", "P%u played ROAD_BUILDING", (unsigned)(ef.a + 1));
            break;
        case EffectKind::YEAR_OF_PLENTY_PLAYED:
            LOGI("DEV", "P%u played YEAR_OF_PLENTY", (unsigned)(ef.a + 1));
            break;
        case EffectKind::MONOPOLY_PLAYED:
            LOGI("DEV", "P%u monopolized res=%u (%u cards)",
                 (unsigned)(ef.a + 1), (unsigned)ef.b, (unsigned)ef.c);
            break;
        case EffectKind::LARGEST_ARMY_CHANGED:
            LOGI("BONUS", "Largest Army -> P%u", (unsigned)(ef.a + 1));
            break;
        case EffectKind::LONGEST_ROAD_CHANGED:
            LOGI("BONUS", "Longest Road -> P%u (len=%u)",
                 (unsigned)(ef.a + 1), (unsigned)ef.b);
            break;
        case EffectKind::PURCHASE_MADE:
            LOGI("BUY", "P%u kind=%u", (unsigned)(ef.a + 1), (unsigned)ef.b);
            break;
        case EffectKind::TRADE_OFFERED:
            LOGI("TRADE", "P%u -> P%u", (unsigned)(ef.a + 1), (unsigned)(ef.b + 1));
            break;
        case EffectKind::TRADE_ACCEPTED:
            LOGI("TRADE", "P%u accepted by P%u",
                 (unsigned)(ef.a + 1), (unsigned)(ef.b + 1));
            break;
        case EffectKind::TRADE_DECLINED:
            LOGI("TRADE", "P%u declined", (unsigned)(ef.a + 1));
            break;
        case EffectKind::TRADE_CANCELLED:
            LOGI("TRADE", "cancelled");
            break;
        case EffectKind::BANK_TRADED:
            LOGI("BANK", "P%u traded with bank", (unsigned)(ef.a + 1));
            break;
        case EffectKind::VP_CHANGED:
            LOGI("VP", "P%u public=%u", (unsigned)(ef.a + 1), (unsigned)ef.b);
            break;
        case EffectKind::WINNER: {
            uint8_t w = ef.a;
            LOGI("WINNER", "P%u reached %u VP", (unsigned)(w + 1), (unsigned)VP_TO_WIN);
            // Schedule a long celebratory flash on every tile (non-blocking).
            uint8_t all[TILE_COUNT];
            for (uint8_t i = 0; i < TILE_COUNT; ++i) all[i] = i;
            led::flashTiles(all, TILE_COUNT, kPlayerColors[w % MAX_PLAYERS], 6, 300);
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Sensor → FSM
// ---------------------------------------------------------------------------
static void pumpSensors() {
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (sensor::vertexChanged(v)) {
            if (sensor::vertexPresent(v)) {
                LOGD("SENSOR", "vertex %u: placed", (unsigned)v);
                sm.onVertexPresent(v);
            } else {
                LOGD("SENSOR", "vertex %u: removed", (unsigned)v);
                sm.onVertexAbsent(v);
            }
        }
    }
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (sensor::edgeChanged(e) && sensor::edgePresent(e)) {
            LOGD("SENSOR", "edge %u: placed", (unsigned)e);
            sm.onEdgePresent(e);
        }
    }
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (sensor::tileChanged(t) && sensor::tilePresent(t)) {
            LOGD("SENSOR", "tile %u: placed", (unsigned)t);
            sm.onTilePresent(t);
        }
    }
}

// ---------------------------------------------------------------------------
// BLE → FSM (PlayerInput) and presence tracking
// ---------------------------------------------------------------------------
static void onPlayerInput(const catan_PlayerInput& in) {
    if (in.player_id >= MAX_PLAYERS) {
        LOGW("BLE", "PlayerInput bad player_id=%u", (unsigned)in.player_id);
        return;
    }
    LOGI("INPUT", "P%u action=%u client='%s'",
         (unsigned)(in.player_id + 1), (unsigned)in.action, in.client_id);

    core::ActionPayload p;
    p.res[0]  = (uint8_t)(in.res_lumber & 0xFF);
    p.res[1]  = (uint8_t)(in.res_wool   & 0xFF);
    p.res[2]  = (uint8_t)(in.res_grain  & 0xFF);
    p.res[3]  = (uint8_t)(in.res_brick  & 0xFF);
    p.res[4]  = (uint8_t)(in.res_ore    & 0xFF);
    p.want[0] = (uint8_t)(in.want_lumber & 0xFF);
    p.want[1] = (uint8_t)(in.want_wool   & 0xFF);
    p.want[2] = (uint8_t)(in.want_grain  & 0xFF);
    p.want[3] = (uint8_t)(in.want_brick  & 0xFF);
    p.want[4] = (uint8_t)(in.want_ore    & 0xFF);
    p.target       = (uint8_t)(in.target_player & 0xFF);
    p.robber_tile  = (uint8_t)(in.robber_tile   & 0xFF);
    p.monopoly_res = (uint8_t)(in.monopoly_res  & 0xFF);
    p.card_res_1   = (uint8_t)(in.card_res_1    & 0xFF);
    p.card_res_2   = (uint8_t)(in.card_res_2    & 0xFF);
    p.aux = 0xFF;

    sm.handlePlayerAction(in.player_id, toActionKind(in.action), p);
}

static void onPresenceChanged(uint8_t mask) {
    uint8_t highest = 0;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        bool c = (mask & (1u << p)) != 0;
        game::setPlayerConnected(p, c);
        if (c) highest = (uint8_t)(p + 1);
        if (!c) game::setPlayerReady(p, false);
    }
    if (game::phase() == GamePhase::LOBBY) {
        game::setNumPlayers(highest);
    }
    LOGI("PRES", "mask=0x%02X num_players=%u phase=%s",
         (unsigned)mask, (unsigned)game::numPlayers(),
         game::phaseName(game::phase()));
}

// ---------------------------------------------------------------------------
// BoardState broadcast
// ---------------------------------------------------------------------------
static void broadcastBoardState() {
    catan_BoardState s = catan_BoardState_init_zero;
    s.proto_version  = CATAN_PROTO_VERSION;
    s.phase          = phaseToProto(game::phase());
    s.num_players    = game::numPlayers();
    s.current_player = game::currentPlayer();
    s.setup_round    = game::setupRound();
    s.has_rolled     = game::hasRolled();
    s.die1           = game::lastDie1();
    s.die2           = game::lastDie2();
    s.reveal_number  = (game::phase() == GamePhase::NUMBER_REVEAL)
                           ? game::currentRevealNumber() : 0;
    s.robber_tile    = (game::robberTile() < TILE_COUNT) ? game::robberTile() : 0xFF;
    s.connected_mask = game::connectedMask();

    uint8_t w = game::checkWinner();
    s.winner_id = (w == NO_PLAYER) ? 0xFF : w;

    s.tiles_packed.size = TILE_COUNT;
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        uint8_t biome_c = biomeCode(g_tile_state[t].biome);
        uint8_t number  = g_tile_state[t].number & 0x0F;
        s.tiles_packed.bytes[t] = (uint8_t)((biome_c << 4) | number);
    }

    s.vp_count = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.vp[p] = game::publicVp(p);

    s.ready_count = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.ready[p] = game::playerReady(p) ? 1 : 0;

    s.res_lumber_count = MAX_PLAYERS;
    s.res_wool_count   = MAX_PLAYERS;
    s.res_grain_count  = MAX_PLAYERS;
    s.res_brick_count  = MAX_PLAYERS;
    s.res_ore_count    = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        s.res_lumber[p] = game::resCount(p, Res::LUMBER);
        s.res_wool[p]   = game::resCount(p, Res::WOOL);
        s.res_grain[p]  = game::resCount(p, Res::GRAIN);
        s.res_brick[p]  = game::resCount(p, Res::BRICK);
        s.res_ore[p]    = game::resCount(p, Res::ORE);
    }

    s.last_reject_reason = game::lastRejectReason();

    // Vertex ownership — 54 vertices / 27 bytes (two nibbles per byte).
    s.vertex_owners.size = 27;
    memset(s.vertex_owners.bytes, 0xFF, 27);
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        const VertexState& vs = game::vertexState(v);
        if (vs.owner == NO_PLAYER) continue;
        uint8_t nib = (uint8_t)(vs.owner & 0x3) | (vs.is_city ? 0x4 : 0x0);
        uint8_t byte_idx = v >> 1;
        if (v & 1) {
            s.vertex_owners.bytes[byte_idx] =
                (s.vertex_owners.bytes[byte_idx] & 0x0F) | (uint8_t)(nib << 4);
        } else {
            s.vertex_owners.bytes[byte_idx] =
                (s.vertex_owners.bytes[byte_idx] & 0xF0) | nib;
        }
    }

    // Edge ownership — 72 edges / 36 bytes.
    s.edge_owners.size = 36;
    memset(s.edge_owners.bytes, 0xFF, 36);
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        const EdgeState& es = game::edgeState(e);
        if (es.owner == NO_PLAYER) continue;
        uint8_t nib = (uint8_t)(es.owner & 0x3);
        uint8_t byte_idx = e >> 1;
        if (e & 1) {
            s.edge_owners.bytes[byte_idx] =
                (s.edge_owners.bytes[byte_idx] & 0x0F) | (uint8_t)(nib << 4);
        } else {
            s.edge_owners.bytes[byte_idx] =
                (s.edge_owners.bytes[byte_idx] & 0xF0) | nib;
        }
    }

    s.dev_cards.size = MAX_PLAYERS * 5;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        s.dev_cards.bytes[p * 5 + 0] = game::devCardCount(p, Dev::KNIGHT);
        s.dev_cards.bytes[p * 5 + 1] = game::devCardCount(p, Dev::VP);
        s.dev_cards.bytes[p * 5 + 2] = game::devCardCount(p, Dev::ROAD_BUILDING);
        s.dev_cards.bytes[p * 5 + 3] = game::devCardCount(p, Dev::YEAR_OF_PLENTY);
        s.dev_cards.bytes[p * 5 + 4] = game::devCardCount(p, Dev::MONOPOLY);
    }
    s.knights_played.size = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.knights_played.bytes[p] = game::knightsPlayed(p);
    s.largest_army_player  = (game::largestArmyPlayer() == NO_PLAYER) ? 0xFF : game::largestArmyPlayer();
    s.longest_road_player  = (game::longestRoadPlayer() == NO_PLAYER) ? 0xFF : game::longestRoadPlayer();
    s.longest_road_length  = game::longestRoadLength();
    s.dev_deck_remaining   = game::devDeckRemaining();
    s.card_played_this_turn = game::cardPlayedThisTurn();

    s.discard_required_mask = game::discardRequiredMask();
    s.discard_required_count.size = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.discard_required_count.bytes[p] = game::discardRequiredCount(p);
    s.steal_eligible_mask = game::stealEligibleMask();

    s.pending_road_buy.size       = MAX_PLAYERS;
    s.pending_settlement_buy.size = MAX_PLAYERS;
    s.pending_city_buy.size       = MAX_PLAYERS;
    s.free_roads_remaining.size   = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        s.pending_road_buy.bytes[p]       = game::pendingRoadBuy(p);
        s.pending_settlement_buy.bytes[p] = game::pendingSettlementBuy(p);
        s.pending_city_buy.bytes[p]       = game::pendingCityBuy(p);
        s.free_roads_remaining.bytes[p]   = game::freeRoadsRemaining(p);
    }

    {
        const auto& t = game::pendingTrade();
        s.trade_from_player = (t.from == NO_PLAYER) ? 0xFF : t.from;
        s.trade_to_player   = (t.to   == NO_PLAYER) ? 0xFF : t.to;
        s.trade_offer.size  = 5;
        s.trade_want.size   = 5;
        for (uint8_t i = 0; i < 5; ++i) {
            s.trade_offer.bytes[i] = t.offer[i];
            s.trade_want.bytes[i]  = t.want[i];
        }
    }

    s.bank_supply.size = 5;
    for (uint8_t r = 0; r < 5; ++r) s.bank_supply.bytes[r] = game::bankSupply((Res)r);
    {
        const uint8_t* dist = game::lastDistribution();
        s.last_distribution.size = MAX_PLAYERS * 5;
        for (uint8_t i = 0; i < MAX_PLAYERS * 5; ++i) s.last_distribution.bytes[i] = dist[i];
    }

    static uint8_t buf[CATAN_MAX_PAYLOAD];
    size_t n = catan_encode_board_state(&s, buf, sizeof(buf));
    if (n == 0) {
        LOGE("BCAST", "BoardState encode fail");
        return;
    }
    game::clearLastRejectReason();
    comms::broadcastBoardState(buf, n);
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------
static void logHeartbeat() {
    const auto& cs = comms::stats();
    LOGI("HB", "phase=%s loops=%lu mask=0x%02X ready=0x%02X "
              "ble conn=%u rx_ok=%lu rx_drop=%lu state_tx=%lu pres_evt=%lu",
         game::phaseName(game::phase()),
         (unsigned long)loop_count,
         (unsigned)game::connectedMask(),
         (unsigned)game::readyMask(),
         (unsigned)comms::connectedCount(),
         (unsigned long)cs.inputs_rx,
         (unsigned long)cs.inputs_dropped,
         (unsigned long)cs.state_notified,
         (unsigned long)cs.presence_events);
}

// =============================================================================
// setup() / loop()
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(150);
    Serial.println();
    Serial.println(F("======================================"));
    Serial.println(F(" Settlers of Catan — ESP32-C6 Board"));
    Serial.print  (F(" Proto v")); Serial.println(CATAN_PROTO_VERSION);
    Serial.println(F("======================================"));

    LOGI("BOOT", "led::init  (data pin=%d, count=%u)",
         LED_DATA_PIN, (unsigned)TOTAL_LED_COUNT);
    led::init();

    LOGI("BOOT", "sensor::init (SDA=%d SCL=%d @%lu Hz)",
         I2C_SDA_PIN, I2C_SCL_PIN, (unsigned long)I2C_BUS_HZ);
    sensor::init();

    // Mix two unrelated entropy sources: ADC analog noise + esp_random()
    // (which uses the on-chip TRNG once Wi-Fi/BT are running).
    uint32_t seed = (uint32_t)analogRead(0) ^ esp_random();
    LOGI("BOOT", "dice::init seed=0x%08lX", (unsigned long)seed);
    dice::init((uint16_t)(seed & 0xFFFF));
    core::rng::seed(seed);

    LOGI("BOOT", "comms::init  (NimBLE peripheral)");
    comms::init();

    LOGI("BOOT", "game::init");
    game::init();
    sm.reset();

    // Power-on indicator: full white until the lobby pattern takes over.
    led::setAllTiles(CRGB::White);
    led::show();
    delay(500);

    game::setPhase(GamePhase::LOBBY);
    LOGI("BOOT", "ready, entering LOBBY (broadcast every %lums)",
         (unsigned long)STATE_BROADCAST_MS);

    if (DEMO_MODE) {
        runDemoFrame();
        LOGI("DEMO", "demo mode ON — cycling tiles every %lums",
             (unsigned long)DEMO_CYCLE_MS);
    }
}

void loop() {
    ++loop_count;
    const uint32_t now = millis();

    if (DEMO_MODE) {
        if (now - last_demo_ms >= DEMO_CYCLE_MS) {
            last_demo_ms = now;
            runDemoFrame();
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_MS));
        return;
    }

    sensor::poll();
    pumpSensors();

    comms::poll(onPlayerInput, onPresenceChanged);
    comms::tick();

    sm.tick(now);

    core::Effect ef;
    while (sm.pollEffect(ef)) applyEffect(ef);

    led::tick(now);

    if (now - last_broadcast_ms >= STATE_BROADCAST_MS) {
        last_broadcast_ms = now;
        broadcastBoardState();
    }

    if (now - last_heartbeat_ms >= HEARTBEAT_MS) {
        last_heartbeat_ms = now;
        logHeartbeat();
    }

    // vTaskDelay yields to the NimBLE host task, FastLED RMT ISR, etc.
    vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_MS));
}
