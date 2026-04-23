// =============================================================================
// main.cpp — Settlers of Catan: Arduino Mega central board controller.
//
// This file is now a thin I/O shell:
//   - It reads hall-effect sensors (debounced in sensor_manager) and feeds
//     presence events into `core::StateMachine`.
//   - It polls the bridge for `PlayerInput` envelopes and forwards them.
//   - On every loop it drains `Effect`s from the StateMachine and applies
//     them to the LED strip, serial log, and comm broadcasts.
//
// All game logic — phase transitions, placement validation, dice, turn
// order, winner checks — lives in `firmware/board/src/core/` and is pure
// (no Arduino, no hardware) so it can be compiled for the native host and
// exercised by `firmware/board/native/sim_main.cpp`.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "catan_wire.h"
#include "catan_log.h"
#include "board_types.h"
#include "board_topology.h"
#include "led_manager.h"
#include "sensor_manager.h"
#include "game_state.h"
#include "dice.h"
#include "comm_manager.h"
#include "proto/catan.pb.h"
#include "core/state_machine.h"
#include "core/events.h"
#include "core/rng.h"

static core::StateMachine sm;

// ── Timing ──────────────────────────────────────────────────────────────────
static constexpr uint32_t HEARTBEAT_MS = 5000;
static uint32_t last_broadcast_ms  = 0;
static uint32_t last_input_poll_ms = 0;
static uint32_t last_heartbeat_ms  = 0;
static uint32_t last_demo_ms       = 0;
static uint32_t loop_count         = 0;

// ── Player colours for LED feedback ────────────────────────────────────────
static const CRGB kPlayerColors[MAX_PLAYERS] = {
    CRGB::Red, CRGB::Blue, CRGB::Orange, CRGB::White
};

// ── Demo Mode ───────────────────────────────────────────────────────────────
// One colour per resource type (matches led_manager biomeColor palette).
static const CRGB kDemoColors[] = {
    CRGB(0,   200,   0),    // FOREST  – green  (Wood)
    CRGB(255, 255,   0),    // PASTURE – yellow (Wool)
    CRGB(255, 165,   0),    // FIELD   – orange (Grain)
    CRGB(255,   0,   0),    // HILL    – red    (Brick)
    CRGB(128,   0, 128),    // MOUNTAIN– purple (Ore)
    CRGB(255, 255, 255),    // DESERT  – white
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
        case PortType::LUMBER_2_1:  return CRGB(0, 100, 0);
        case PortType::WOOL_2_1:    return CRGB(144, 238, 144);
        case PortType::GRAIN_2_1:   return CRGB(255, 215, 0);
        case PortType::BRICK_2_1:   return CRGB(178, 34, 34);
        case PortType::ORE_2_1:     return CRGB(128, 128, 128);
        case PortType::GENERIC_3_1: return CRGB::White;
        default:                    return CRGB::Black;
    }
}

// Map internal Biome (board_types.h) → packed biome code in BoardState.
static uint8_t biomeCode(Biome b) {
    switch (b) {
        case Biome::FOREST:   return 1;
        case Biome::PASTURE:  return 2;
        case Biome::FIELD:    return 3;
        case Biome::HILL:     return 4;
        case Biome::MOUNTAIN: return 5;
        default:              return 0;  // DESERT
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
        case GamePhase::GAME_OVER:         return catan_GamePhase_PHASE_GAME_OVER;
    }
    return catan_GamePhase_PHASE_LOBBY;
}

// Translate nanopb PlayerAction enum → core::ActionKind.
// Values are defined identically (0..9) but we cast explicitly so a future
// divergence is caught.
static core::ActionKind toActionKind(catan_PlayerAction a) {
    switch (a) {
        case catan_PlayerAction_ACTION_READY:        return core::ActionKind::READY;
        case catan_PlayerAction_ACTION_START_GAME:   return core::ActionKind::START_GAME;
        case catan_PlayerAction_ACTION_NEXT_NUMBER:  return core::ActionKind::NEXT_NUMBER;
        case catan_PlayerAction_ACTION_PLACE_DONE:   return core::ActionKind::PLACE_DONE;
        case catan_PlayerAction_ACTION_ROLL_DICE:    return core::ActionKind::ROLL_DICE;
        case catan_PlayerAction_ACTION_END_TURN:     return core::ActionKind::END_TURN;
        case catan_PlayerAction_ACTION_SKIP_ROBBER:  return core::ActionKind::SKIP_ROBBER;
        case catan_PlayerAction_ACTION_REPORT:       return core::ActionKind::REPORT;
        case catan_PlayerAction_ACTION_REQUEST_SYNC: return core::ActionKind::REQUEST_SYNC;
        default:                                     return core::ActionKind::NONE;
    }
}

// ---------------------------------------------------------------------------
// Effect → hardware side-effects
// ---------------------------------------------------------------------------

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
        default:                                           return "NONE";
    }
}

static void applyEffect(const core::Effect& ef) {
    using core::EffectKind;
    switch (ef.kind) {
        case EffectKind::PHASE_ENTERED: {
            GamePhase p = (GamePhase)ef.a;
            LOGI("FSM", "phase -> %s", game::phaseName(p));
            if (p == GamePhase::PLAYING) {
                led::colorAllTilesByBiome();
                if (game::robberTile() < TILE_COUNT) led::dimTile(game::robberTile());
                led::show();
            }
            break;
        }
        case EffectKind::LOBBY_MASK_CHANGED: {
            uint8_t mask = ef.a;
            led::setAllTiles(CRGB(20, 20, 40));
            for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                if (mask & (1 << i)) led::setTileColor(i, kPlayerColors[i]);
            }
            led::show();
            LOGI("LOBBY", "connected mask=0x%02X (%u players)",
                 (unsigned)mask, (unsigned)game::numPlayers());
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
        case EffectKind::PLACED_ROAD: {
            uint8_t p = ef.a, e = ef.b;
            LOGI("PLACE", "P%u road e%u", (unsigned)(p + 1), (unsigned)e);
            break;
        }
        case EffectKind::PLACEMENT_REJECTED: {
            LOGW("REJECT", "P%u reason=%s", (unsigned)(ef.a + 1),
                 rejectReasonName((core::RejectReason)ef.b));
            break;
        }
        case EffectKind::DICE_ROLLED: {
            uint8_t d1 = ef.a, d2 = ef.b;
            uint8_t total = (uint8_t)(d1 + d2);
            LOGI("DICE", "P%u rolled %u+%u=%u%s",
                 (unsigned)(game::currentPlayer() + 1),
                 (unsigned)d1, (unsigned)d2, (unsigned)total,
                 ef.c ? " -> ROBBER" : "");
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
        case EffectKind::WINNER: {
            uint8_t w = ef.a;
            LOGI("WINNER", "P%u reached %u VP", (unsigned)(w + 1), (unsigned)VP_TO_WIN);
            for (uint8_t i = 0; i < 5; ++i) {
                led::setAllTiles(kPlayerColors[w % MAX_PLAYERS]);
                led::show();
                delay(300);
                led::setAllTiles(CRGB::Black);
                led::show();
                delay(300);
            }
            led::colorAllTilesByBiome();
            led::show();
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Sensor pump — translates debounced presence changes into StateMachine events
// ---------------------------------------------------------------------------
static void pumpSensors() {
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (sensor::vertexChanged(v) && sensor::vertexPresent(v)) {
            sm.onVertexPresent(v);
        }
    }
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (sensor::edgeChanged(e) && sensor::edgePresent(e)) {
            sm.onEdgePresent(e);
        }
    }
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (sensor::tileChanged(t) && sensor::tilePresent(t)) {
            sm.onTilePresent(t);
        }
    }
}

// ---------------------------------------------------------------------------
// Player input pump — decode incoming PlayerInputs and forward to FSM
// ---------------------------------------------------------------------------
static void pumpPlayerInputs() {
    for (uint8_t i = 0; i < 4; ++i) {
        catan_PlayerInput in = catan_PlayerInput_init_zero;
        uint32_t sender = 0;
        if (!comm::pollPlayerInput(in, sender)) return;
        if (in.player_id >= MAX_PLAYERS) {
            LOGW("INPUT", "drop bad player_id=%u from sender=%lu",
                 (unsigned)in.player_id, (unsigned long)sender);
            continue;
        }
        LOGI("INPUT", "P%u action=%u vp=%u (sender=%lu)",
             (unsigned)(in.player_id + 1), (unsigned)in.action,
             (unsigned)in.vp, (unsigned long)sender);
        sm.handlePlayerAction(in.player_id, toActionKind(in.action), (uint8_t)in.vp);
    }
}

// ---------------------------------------------------------------------------
// BoardState broadcast
// ---------------------------------------------------------------------------
static void broadcastBoardState() {
    catan_BoardState s = catan_BoardState_init_zero;
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

    s.vp_count = game::numPlayers();
    for (uint8_t p = 0; p < s.vp_count && p < 4; ++p) {
        s.vp[p] = game::reportedVp(p);
    }

    comm::sendBoardState(s);
}

// =============================================================================
// setup() / loop()
// =============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { ; }

    Serial.println();
    Serial.println(F("======================================"));
    Serial.println(F(" Settlers of Catan - Board Controller"));
    Serial.print  (F(" Proto v"));
    Serial.println(CATAN_PROTO_VERSION);
    Serial.println(F("======================================"));

    LOGI("BOOT", "Wire.begin() @100kHz");
    Serial.flush();
    Wire.begin();
    Wire.setClock(100000);
    // AVR Wire has no default timeout: a missing/broken I2C bus or a dead
    // expander will hang requestFrom/endTransmission forever. Give it a
    // 25ms ceiling so boot can always progress.
    Wire.setWireTimeout(25000UL, /*reset_on_timeout=*/true);

    LOGI("BOOT", "led::init");
    Serial.flush();
    led::init();

    LOGI("BOOT", "sensor::init (probe %u expanders)", (unsigned)EXPANDER_COUNT);
    Serial.flush();
    {
        uint8_t found = 0;
        for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
            Wire.beginTransmission(EXPANDER_ADDRS[i]);
            uint8_t rc = Wire.endTransmission();
            if (rc == 0) {
                LOGI("I2C", "  expander 0x%02X OK", (unsigned)EXPANDER_ADDRS[i]);
                found++;
            } else {
                LOGW("I2C", "  expander 0x%02X missing (rc=%u)",
                     (unsigned)EXPANDER_ADDRS[i], (unsigned)rc);
            }
            Serial.flush();
        }
        LOGI("I2C", "probe done: %u/%u expanders present",
             (unsigned)found, (unsigned)EXPANDER_COUNT);
    }
    sensor::init();

    uint16_t dice_seed = (uint16_t)analogRead(A0);
    LOGI("BOOT", "dice::init seed=%u", (unsigned)dice_seed);
    Serial.flush();
    dice::init(dice_seed);

    LOGI("BOOT", "comm::init (bridge UART)");
    Serial.flush();
    comm::init();

    LOGI("BOOT", "game::init");
    Serial.flush();
    game::init();
    sm.reset();

    led::setAllTiles(CRGB(20, 20, 40));
    led::show();

    game::setPhase(GamePhase::LOBBY);
    LOGI("BOOT", "ready, entering LOBBY (broadcast every %lums)",
         (unsigned long)STATE_BROADCAST_MS);

    if (DEMO_MODE) {
        runDemoFrame();
        LOGI("DEMO", "demo mode ON — cycling tiles every %lums", (unsigned long)DEMO_CYCLE_MS);
    }
}

static void logHeartbeat() {
    const comm::Stats& s = comm::stats();
    LOGI("HB", "phase=%s loops=%lu bs_tx=%lu tx_bytes=%lu rx_bytes=%lu rx_ok=%lu rx_bad=%lu rx_dup=%lu",
         game::phaseName(game::phase()),
         (unsigned long)loop_count,
         (unsigned long)s.tx_boardstate,
         (unsigned long)s.tx_bytes,
         (unsigned long)s.rx_bytes,
         (unsigned long)s.rx_frames_ok,
         (unsigned long)s.rx_frames_bad,
         (unsigned long)s.rx_dups);
}

void loop() {
    loop_count++;

    if (DEMO_MODE) {
        if (millis() - last_demo_ms >= DEMO_CYCLE_MS) {
            last_demo_ms = millis();
            runDemoFrame();
        }
        delay(SENSOR_POLL_MS);
        return;
    }

    sensor::poll();
    pumpSensors();

    if (millis() - last_input_poll_ms >= INPUT_POLL_MS) {
        last_input_poll_ms = millis();
        pumpPlayerInputs();
    }

    sm.tick(millis());

    // Drain all pending effects before the next iteration.
    core::Effect ef;
    while (sm.pollEffect(ef)) applyEffect(ef);

    if (millis() - last_broadcast_ms >= STATE_BROADCAST_MS) {
        last_broadcast_ms = millis();
        broadcastBoardState();
    }

    if (millis() - last_heartbeat_ms >= HEARTBEAT_MS) {
        last_heartbeat_ms = millis();
        logHeartbeat();
    }

    delay(SENSOR_POLL_MS);
}
