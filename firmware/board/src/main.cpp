// =============================================================================
// main.cpp — Settlers of Catan: Arduino Mega central board controller.
//
//   Mega  <--I2C(100k)-->  PCF8574 sensor expanders   (game presence input)
//   Mega  <--Serial1-->    ESP32-C6 BLE hub           (player I/O)
//
// The Mega owns the game FSM, sensors, and LEDs. The hub owns BLE — it
// reports who is connected (PlayerPresence) and forwards each phone's
// PlayerInput. The Mega broadcasts BoardState to the hub on a fixed
// 200 ms cadence; the hub fans those out to every connected mobile.
//
// All game logic — phases, placement, dice, turn order — lives in
// firmware/board/src/core/ and is host-compilable for native simulation
// (see firmware/board/native/sim_main.cpp).
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "catan_log.h"
#include "catan_wire.h"
#include "link.h"
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
static constexpr uint32_t HEARTBEAT_MS = 5000;
static uint32_t last_broadcast_ms  = 0;
static uint32_t last_heartbeat_ms  = 0;
static uint32_t last_demo_ms       = 0;
static uint32_t loop_count         = 0;

// ── Player colours for LED feedback ────────────────────────────────────────
static const CRGB kPlayerColors[MAX_PLAYERS] = {
    CRGB::Red, CRGB::Blue, CRGB::Orange, CRGB::White
};

// ── Demo Mode ───────────────────────────────────────────────────────────────
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
        case PortType::LUMBER_2_1:  return CRGB(0, 200, 0);    // matches FOREST biome
        case PortType::WOOL_2_1:    return CRGB(255, 255, 0);  // matches PASTURE biome
        case PortType::GRAIN_2_1:   return CRGB(255, 165, 0);  // matches FIELD biome
        case PortType::BRICK_2_1:   return CRGB(255, 0, 0);    // matches HILL biome
        case PortType::ORE_2_1:     return CRGB(128, 0, 128);  // matches MOUNTAIN biome
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
        case GamePhase::GAME_OVER:         return catan_GamePhase_PHASE_GAME_OVER;
    }
    return catan_GamePhase_PHASE_LOBBY;
}

static core::ActionKind toActionKind(catan_PlayerAction a) {
    switch (a) {
        case catan_PlayerAction_ACTION_READY:       return core::ActionKind::READY;
        case catan_PlayerAction_ACTION_START_GAME:  return core::ActionKind::START_GAME;
        case catan_PlayerAction_ACTION_NEXT_NUMBER: return core::ActionKind::NEXT_NUMBER;
        case catan_PlayerAction_ACTION_PLACE_DONE:  return core::ActionKind::PLACE_DONE;
        case catan_PlayerAction_ACTION_ROLL_DICE:   return core::ActionKind::ROLL_DICE;
        case catan_PlayerAction_ACTION_END_TURN:    return core::ActionKind::END_TURN;
        case catan_PlayerAction_ACTION_SKIP_ROBBER: return core::ActionKind::SKIP_ROBBER;
        case catan_PlayerAction_ACTION_REPORT:      return core::ActionKind::REPORT;
        default:                                    return core::ActionKind::NONE;
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

static void broadcastBoardState();

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
            LOGI("LOBBY", "connected mask=0x%02X (%u players, ready=0x%02X)",
                 (unsigned)mask, (unsigned)game::numPlayers(),
                 (unsigned)game::readyMask());
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
            // Broadcast immediately so the phone sees the result at the same
            // moment the board starts flashing, not after the blocking flash.
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
// UART mega_link → game (PlayerInput + PlayerPresence dispatch)
// ---------------------------------------------------------------------------
static void handlePlayerInputFrame(const uint8_t* payload, uint8_t len) {
    catan_PlayerInput in = catan_PlayerInput_init_zero;
    if (!catan_decode_player_input(payload, len, &in)) {
        LOGW("LINK", "PlayerInput decode fail (len=%u)", (unsigned)len);
        return;
    }
    if (in.player_id >= MAX_PLAYERS) {
        LOGW("LINK", "PlayerInput bad player_id=%u", (unsigned)in.player_id);
        return;
    }
    LOGI("INPUT", "P%u action=%u vp=%u client='%s'",
         (unsigned)(in.player_id + 1), (unsigned)in.action,
         (unsigned)in.vp, in.client_id);

    // Forward REPORT vp via the dedicated REPORT path; for READY we
    // overload `vp` as the new ready bit (1 = ready, 0 = unready).
    uint8_t aux_vp = in.vp & 0xFF;
    if (in.action == catan_PlayerAction_ACTION_READY) {
        // The app sends ACTION_READY with no payload — treat as toggle:
        // if vp==0 it means "set unready", any non-zero means "set ready".
        // For simplicity we toggle here so the same button works both ways.
        if (in.vp == 0) aux_vp = game::playerReady(in.player_id) ? 0 : 1;
    }
    // Persist self-reported resources so reconnecting clients can restore them.
    if (in.action == catan_PlayerAction_ACTION_REPORT) {
        game::setReportedRes(in.player_id, 0, (uint8_t)(in.res_lumber & 0xFF));
        game::setReportedRes(in.player_id, 1, (uint8_t)(in.res_wool   & 0xFF));
        game::setReportedRes(in.player_id, 2, (uint8_t)(in.res_grain  & 0xFF));
        game::setReportedRes(in.player_id, 3, (uint8_t)(in.res_brick  & 0xFF));
        game::setReportedRes(in.player_id, 4, (uint8_t)(in.res_ore    & 0xFF));
    }
    sm.handlePlayerAction(in.player_id, toActionKind(in.action), aux_vp);
}

static void handlePresenceFrame(const uint8_t* payload, uint8_t len) {
    catan_PlayerPresence pres = catan_PlayerPresence_init_zero;
    if (!catan_decode_player_presence(payload, len, &pres)) {
        LOGW("LINK", "PlayerPresence decode fail (len=%u)", (unsigned)len);
        return;
    }
    uint8_t mask = (uint8_t)(pres.connected_mask & 0x0F);
    uint8_t highest = 0;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        bool c = (mask & (1u << p)) != 0;
        game::setPlayerConnected(p, c);
        if (c) highest = (uint8_t)(p + 1);
        if (!c) game::setPlayerReady(p, false);   // disconnect drops ready
    }
    game::setNumPlayers(highest);
    LOGI("PRES", "mask=0x%02X num_players=%u", (unsigned)mask, (unsigned)highest);
}

static void onLinkFrame(uint8_t type, const uint8_t* payload, uint8_t len) {
    switch (type) {
        case CATAN_MSG_PLAYER_INPUT:    handlePlayerInputFrame(payload, len); break;
        case CATAN_MSG_PLAYER_PRESENCE: handlePresenceFrame(payload, len);    break;
        default:
            LOGW("LINK", "unknown frame type=0x%02X len=%u", type, len);
            break;
    }
}

// ---------------------------------------------------------------------------
// BoardState broadcast (Mega -> hub -> mobiles)
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
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.vp[p] = game::reportedVp(p);

    s.ready_count = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.ready[p] = game::playerReady(p) ? 1 : 0;

    s.res_lumber_count = MAX_PLAYERS;
    s.res_wool_count   = MAX_PLAYERS;
    s.res_grain_count  = MAX_PLAYERS;
    s.res_brick_count  = MAX_PLAYERS;
    s.res_ore_count    = MAX_PLAYERS;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        s.res_lumber[p] = game::reportedRes(p, 0);
        s.res_wool[p]   = game::reportedRes(p, 1);
        s.res_grain[p]  = game::reportedRes(p, 2);
        s.res_brick[p]  = game::reportedRes(p, 3);
        s.res_ore[p]    = game::reportedRes(p, 4);
    }

    // Single-shot placement rejection for the current player.
    s.last_reject_reason = game::lastRejectReason();

    // Vertex ownership — 54 vertices packed as 27 bytes (two nibbles per byte).
    //   nibble 0x0..0x3 = settlement P0..P3
    //   nibble 0x4..0x7 = city       P0..P3 (owner = nibble & 0x3)
    //   nibble 0xF      = empty
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

    // Edge ownership — 72 edges packed as 36 bytes (two nibbles per byte).
    //   nibble 0x0..0x3 = road P0..P3
    //   nibble 0xF      = empty
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

    uint8_t buf[CATAN_MAX_PAYLOAD];
    size_t n = catan_encode_board_state(&s, buf, sizeof(buf));
    if (n == 0) {
        LOGE("BCAST", "BoardState encode fail");
        return;
    }
    // Clear the single-shot rejection field after it has been encoded so
    // subsequent broadcasts carry 0 (no rejection).
    game::clearLastRejectReason();
    mega_link::send(CATAN_MSG_BOARD_STATE, buf, (uint8_t)n);
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

    LOGI("BOOT", "Wire.begin() @100kHz (sensor bus only)");
    Serial.flush();
    Wire.begin();
    Wire.setClock(100000);
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

    LOGI("BOOT", "mega_link::init (Serial1 to BLE hub)");
    Serial.flush();
    mega_link::init(onLinkFrame);

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
        LOGI("DEMO", "demo mode ON — cycling tiles every %lums",
             (unsigned long)DEMO_CYCLE_MS);
    }
}

static void logHeartbeat() {
    const mega_link::Stats& s = mega_link::stats();
    LOGI("HB", "phase=%s loops=%lu mask=0x%02X ready=0x%02X uart rx=%lu fr=%lu bad_crc=%lu tx=%lu drop=%lu",
         game::phaseName(game::phase()),
         (unsigned long)loop_count,
         (unsigned)game::connectedMask(),
         (unsigned)game::readyMask(),
         (unsigned long)s.rx_bytes,
         (unsigned long)s.rx_frames,
         (unsigned long)s.rx_bad_crc,
         (unsigned long)s.tx_frames,
         (unsigned long)s.tx_dropped);
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

    mega_link::poll();    // dispatches PlayerInput + PlayerPresence

    sm.tick(millis());

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
