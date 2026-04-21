// =============================================================================
// main.cpp — Settlers of Catan: Arduino Mega central board controller.
//
// Responsibilities:
//   - Own the authoritative game phase, turn order, piece placement,
//     board layout (biomes + numbers), the robber, and dice rolls.
//   - Drive all tile/port LEDs.
//   - Read hall-effect sensors (direct GPIO + PCF8574 I2C expanders).
//   - Publish BoardState snapshots to the bridge over I2C (master).
//   - Consume PlayerInput frames returned by the bridge (master reads).
//
// The bridge (Heltec V3, address BRIDGE_I2C_ADDR=0x30) is the only I2C
// slave on the downstream side.  The bridge handles all LoRa communication
// with the player stations.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "catan_wire.h"
#include "board_types.h"
#include "board_topology.h"
#include "led_manager.h"
#include "sensor_manager.h"
#include "game_state.h"
#include "dice.h"
#include "comm_manager.h"
#include "proto/catan.pb.h"

// ── Forward declarations ────────────────────────────────────────────────────
static void handleLobby();
static void handleBoardSetup();
static void handleNumberReveal();
static void handleInitialPlacement();
static void handlePlaying();
static void handleRobber();
static void handleGameOver();

static void consumePlayerInputs();
static void broadcastBoardState();

// ── State ───────────────────────────────────────────────────────────────────
static uint32_t last_broadcast_ms = 0;
static uint32_t last_input_poll_ms = 0;

// Latest semantic input captured for the current player (cleared after use).
static catan_PlayerAction pending_current_action = catan_PlayerAction_ACTION_NONE;
// Any player can trigger these transitions — set when seen, cleared on use.
static bool pending_start_game   = false;
static bool pending_next_number  = false;
// Set to false whenever BOARD_SETUP is entered; guards one-shot board init.
static bool board_setup_done     = false;

// Randomly chosen starting player (set when START_GAME is accepted in LOBBY).
static uint8_t s_first_player = 0;

static const CRGB kPlayerColors[MAX_PLAYERS] = {
    CRGB::Red, CRGB::Blue, CRGB::Orange, CRGB::White
};

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
// Must match the mobile app / player-station decoder (see catan_wire.h docs).
//   0 = DESERT, 1 = FOREST, 2 = PASTURE, 3 = FIELD, 4 = HILL, 5 = MOUNTAIN
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

// Map GamePhase (class enum) → catan_GamePhase.
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

// =============================================================================
// setup()
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

    Wire.begin();                  // I2C master (bridge + expanders)
    Wire.setClock(100000);

    led::init();
    sensor::init();
    dice::init(analogRead(A0));
    comm::init();
    game::init();

    led::setAllTiles(CRGB(20, 20, 40));
    led::show();

    game::setPhase(GamePhase::LOBBY);
    Serial.println(F("[BOOT] Entering LOBBY"));
}

// =============================================================================
// loop()
// =============================================================================

void loop() {
    sensor::poll();

    // Poll bridge for player input
    if (millis() - last_input_poll_ms >= INPUT_POLL_MS) {
        last_input_poll_ms = millis();
        consumePlayerInputs();
    }

    switch (game::phase()) {
        case GamePhase::LOBBY:             handleLobby();            break;
        case GamePhase::BOARD_SETUP:       handleBoardSetup();       break;
        case GamePhase::NUMBER_REVEAL:     handleNumberReveal();     break;
        case GamePhase::INITIAL_PLACEMENT: handleInitialPlacement(); break;
        case GamePhase::PLAYING:           handlePlaying();          break;
        case GamePhase::ROBBER:            handleRobber();           break;
        case GamePhase::GAME_OVER:         handleGameOver();         break;
    }

    // Periodic broadcast to bridge → players → mobile
    if (millis() - last_broadcast_ms >= STATE_BROADCAST_MS) {
        last_broadcast_ms = millis();
        broadcastBoardState();
    }

    delay(SENSOR_POLL_MS);
}

// =============================================================================
// consumePlayerInputs() — drain all pending PlayerInput frames from bridge
// =============================================================================

static void consumePlayerInputs() {
    // Drain up to 4 frames per poll so we don't starve the game loop.
    for (uint8_t i = 0; i < 4; ++i) {
        catan_PlayerInput in = catan_PlayerInput_init_zero;
        if (!comm::pollPlayerInput(in)) return;  // no more pending

        if (in.player_id >= MAX_PLAYERS) continue;

        Serial.print(F("[INPUT] P"));
        Serial.print(in.player_id + 1);
        Serial.print(F(" action="));
        Serial.println((uint8_t)in.action);

        // Mark the sender as connected
        if (!game::playerConnected(in.player_id)) {
            game::setPlayerConnected(in.player_id, true);
            // Bump num_players if a new slot is now occupied
            uint8_t new_n = 0;
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                if (game::playerConnected(p)) new_n = p + 1;
            }
            if (new_n > game::numPlayers()) game::setNumPlayers(new_n);
            Serial.print(F("[CONNECT] P"));
            Serial.print(in.player_id + 1);
            Serial.print(F(" online (total "));
            Serial.print(game::numPlayers());
            Serial.println(F(")"));
        }

        // ACTION_REPORT: record self-reported VP
        if (in.action == catan_PlayerAction_ACTION_REPORT) {
            game::setReportedVp(in.player_id, (uint8_t)in.vp);
        }

        // Global actions (any player can trigger)
        if (in.action == catan_PlayerAction_ACTION_START_GAME)
            pending_start_game = true;
        if (in.action == catan_PlayerAction_ACTION_NEXT_NUMBER)
            pending_next_number = true;

        // Per-current-player actions: capture last one
        if (in.player_id == game::currentPlayer()) {
            if (in.action != catan_PlayerAction_ACTION_NONE &&
                in.action != catan_PlayerAction_ACTION_READY &&
                in.action != catan_PlayerAction_ACTION_REPORT) {
                pending_current_action = in.action;
            }
        }
    }
}

// =============================================================================
// broadcastBoardState() — encode + send current state to bridge
// =============================================================================

static void broadcastBoardState() {
    catan_BoardState s = catan_BoardState_init_zero;
    s.proto_version   = CATAN_PROTO_VERSION;
    s.phase           = phaseToProto(game::phase());
    s.num_players     = game::numPlayers();
    s.current_player  = game::currentPlayer();
    s.setup_round     = game::setupRound();
    s.has_rolled      = game::hasRolled();
    s.die1            = game::lastDie1();
    s.die2            = game::lastDie2();
    s.reveal_number   = (game::phase() == GamePhase::NUMBER_REVEAL)
                          ? game::currentRevealNumber() : 0;
    s.robber_tile     = (game::robberTile() < TILE_COUNT) ? game::robberTile() : 0xFF;
    s.connected_mask  = game::connectedMask();

    uint8_t w = game::checkWinner();
    s.winner_id = (w == NO_PLAYER) ? 0xFF : w;

    // Pack tiles into bytes: high nibble = biome, low nibble = number
    s.tiles_packed.size = TILE_COUNT;
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        uint8_t biome_code = biomeCode(g_tile_state[t].biome);
        uint8_t number     = g_tile_state[t].number & 0x0F;
        s.tiles_packed.bytes[t] = (uint8_t)((biome_code << 4) | number);
    }

    // VP array for connected players
    s.vp_count = game::numPlayers();
    for (uint8_t p = 0; p < s.vp_count && p < 4; ++p) {
        s.vp[p] = game::reportedVp(p);
    }

    comm::sendBoardState(s);
}

// =============================================================================
// Phase: LOBBY
// =============================================================================

static void handleLobby() {
    // Show player slots on the board with their colors
    static uint8_t last_mask = 0xFF;
    uint8_t mask = game::connectedMask();
    if (mask != last_mask) {
        last_mask = mask;
        led::setAllTiles(CRGB(20, 20, 40));
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (mask & (1 << i)) led::setTileColor(i, kPlayerColors[i]);
        }
        led::show();
        Serial.print(F("[LOBBY] connected mask=0x"));
        Serial.println(mask, HEX);
    }

    if (pending_start_game && game::numPlayers() >= MIN_PLAYERS) {
        pending_start_game = false;

        // Pick a random starting player from the currently connected set.
        uint8_t ids[MAX_PLAYERS];
        uint8_t cnt = 0;
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (game::playerConnected(i)) ids[cnt++] = i;
        }
        s_first_player = (cnt > 0) ? ids[random(cnt)] : 0;

        Serial.print(F("[LOBBY] Starting with "));
        Serial.print(game::numPlayers());
        Serial.print(F(" players, first=P"));
        Serial.println(s_first_player + 1);
        board_setup_done = false;
        game::setPhase(GamePhase::BOARD_SETUP);
    }
}

// =============================================================================
// Phase: BOARD_SETUP
// =============================================================================

static void handleBoardSetup() {
    // Run tile/LED initialisation only once when we first enter this phase.
    if (!board_setup_done) {
        board_setup_done = true;
        Serial.println(F("[SETUP] Randomizing board..."));
        randomizeBoardLayout();

        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            if (g_tile_state[t].biome == Biome::DESERT) {
                game::setRobberTile(t);
                break;
            }
        }

        led::colorAllTilesByBiome();
        for (uint8_t p = 0; p < PORT_COUNT; ++p) {
            led::setPortColor(p, portColor(PORT_TOPO[p].type));
        }
        led::show();

        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            Serial.print(F("[TILE] "));
            Serial.print(t);
            Serial.print(F(" "));
            Serial.print(biomeName(g_tile_state[t].biome));
            if (g_tile_state[t].number > 0) {
                Serial.print(F(" #"));
                Serial.print(g_tile_state[t].number);
            }
            Serial.println();
        }

        Serial.println(F("[SETUP] Board ready — waiting for player to start reveal."));
    }

    // Wait for any player to send NEXT_NUMBER before entering number reveal.
    if (pending_next_number) {
        pending_next_number = false;
        game::resetReveal();
        game::setPhase(GamePhase::NUMBER_REVEAL);
        Serial.println(F("[SETUP] → NUMBER_REVEAL"));
    }
}

// =============================================================================
// Phase: NUMBER_REVEAL
// =============================================================================

static void handleNumberReveal() {
    static uint8_t last_shown = 0;
    uint8_t num = game::currentRevealNumber();
    if (num != last_shown) {
        last_shown = num;
        led::setAllTiles(CRGB::Black);
        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            if (g_tile_state[t].number == num) led::setTileColor(t, CRGB::White);
        }
        led::show();
        Serial.print(F("[REVEAL] "));
        Serial.println(num);
    }

    if (pending_next_number) {
        pending_next_number = false;
        if (!game::advanceReveal()) {
            last_shown = 0;
            led::colorAllTilesByBiome();
            if (game::robberTile() < TILE_COUNT) led::dimTile(game::robberTile());
            led::show();

            // Start snake draft with the randomly chosen first player.
            game::resetSetupRound(s_first_player);
            game::setPhase(GamePhase::INITIAL_PLACEMENT);
            Serial.print(F("[REVEAL] → INITIAL_PLACEMENT, P"));
            Serial.print(s_first_player + 1);
            Serial.println(F(" goes first"));
        }
    }
}

// =============================================================================
// Phase: INITIAL_PLACEMENT
// =============================================================================

static void handleInitialPlacement() {
    uint8_t cp = game::currentPlayer();

    // Sensor-driven piece placement
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (!sensor::vertexChanged(v) || !sensor::vertexPresent(v)) continue;
        if (game::vertexState(v).owner == NO_PLAYER) {
            game::placeSettlement(v, cp);
            Serial.print(F("[PLACE] P"));
            Serial.print(cp + 1);
            Serial.print(F(" settlement v"));
            Serial.println(v);
            uint8_t adj[3];
            uint8_t cnt = tilesForVertex(v, adj, 3);
            if (cnt > 0) led::flashTiles(adj, cnt, kPlayerColors[cp], 2, 150);
        }
    }
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (!sensor::edgeChanged(e) || !sensor::edgePresent(e)) continue;
        if (game::edgeState(e).owner == NO_PLAYER) {
            game::placeRoad(e, cp);
            Serial.print(F("[PLACE] P"));
            Serial.print(cp + 1);
            Serial.print(F(" road e"));
            Serial.println(e);
        }
    }

    if (pending_current_action == catan_PlayerAction_ACTION_PLACE_DONE) {
        pending_current_action = catan_PlayerAction_ACTION_NONE;
        Serial.print(F("[SETUP] P"));
        Serial.print(cp + 1);
        Serial.print(F(" done (turn "));
        Serial.print(game::setupTurn());
        Serial.println(F(")"));

        if (!game::advanceSetupTurn()) {
            // All 2*N setup turns complete — start play from the first player.
            game::setCurrentPlayer(s_first_player);
            game::clearDice();
            game::setPhase(GamePhase::PLAYING);
            Serial.print(F("[SETUP] → PLAYING, P"));
            Serial.print(s_first_player + 1);
            Serial.println(F(" goes first"));
        } else {
            Serial.print(F("[SETUP] → P"));
            Serial.print(game::currentPlayer() + 1);
            Serial.print(F(" round "));
            Serial.println(game::setupRound());
        }
    }
}

// =============================================================================
// Phase: PLAYING
// =============================================================================

static void handlePlaying() {
    uint8_t cp = game::currentPlayer();

    // Roll
    if (pending_current_action == catan_PlayerAction_ACTION_ROLL_DICE &&
        !game::hasRolled()) {
        pending_current_action = catan_PlayerAction_ACTION_NONE;
        game::rollDice();
        uint8_t d1 = game::lastDie1();
        uint8_t d2 = game::lastDie2();
        uint8_t total = game::lastDiceTotal();

        Serial.print(F("[DICE] P"));
        Serial.print(cp + 1);
        Serial.print(F(": "));
        Serial.print(d1);
        Serial.print(F("+"));
        Serial.print(d2);
        Serial.print(F("="));
        Serial.println(total);

        uint8_t matching[TILE_COUNT];
        uint8_t match_count = 0;
        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            if (g_tile_state[t].number == total && t != game::robberTile()) {
                matching[match_count++] = t;
            }
        }
        if (match_count > 0)
            led::flashTiles(matching, match_count, CRGB::White, 3, 200);

        if (total == ROBBER_ROLL) {
            Serial.println(F("[DICE] Seven rolled!"));
            game::setPhase(GamePhase::ROBBER);
            return;
        }
    }

    // End turn
    if (pending_current_action == catan_PlayerAction_ACTION_END_TURN &&
        game::hasRolled()) {
        pending_current_action = catan_PlayerAction_ACTION_NONE;
        Serial.print(F("[TURN] P"));
        Serial.print(cp + 1);
        Serial.println(F(" ends turn"));
        game::nextTurn();
    }

    // Piece building via sensors
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (!sensor::vertexChanged(v) || !sensor::vertexPresent(v)) continue;
        const VertexState& vs = game::vertexState(v);
        if (vs.owner == NO_PLAYER) {
            game::placeSettlement(v, cp);
            Serial.print(F("[BUILD] P"));
            Serial.print(cp + 1);
            Serial.print(F(" settlement v"));
            Serial.println(v);
            uint8_t adj[3];
            uint8_t cnt = tilesForVertex(v, adj, 3);
            if (cnt > 0) led::flashTiles(adj, cnt, kPlayerColors[cp], 2, 150);
        } else if (vs.owner == cp && !vs.is_city) {
            game::upgradeToCity(v);
            Serial.print(F("[BUILD] P"));
            Serial.print(cp + 1);
            Serial.print(F(" city v"));
            Serial.println(v);
        }
    }
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (!sensor::edgeChanged(e) || !sensor::edgePresent(e)) continue;
        if (game::edgeState(e).owner == NO_PLAYER) {
            game::placeRoad(e, cp);
            Serial.print(F("[BUILD] P"));
            Serial.print(cp + 1);
            Serial.print(F(" road e"));
            Serial.println(e);
        }
    }

    // Robber can be manually moved outside of 7-roll
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (!sensor::tileChanged(t) || !sensor::tilePresent(t)) continue;
        if (t != game::robberTile()) {
            uint8_t old = game::robberTile();
            if (old < TILE_COUNT) led::undimTile(old);
            game::setRobberTile(t);
            led::dimTile(t);
            led::show();
            Serial.print(F("[ROBBER] Moved to tile "));
            Serial.println(t);
        }
    }

    // Winner check based on self-reported VP
    if (game::checkWinner() != NO_PLAYER) {
        game::setPhase(GamePhase::GAME_OVER);
    }
}

// =============================================================================
// Phase: ROBBER
// =============================================================================

static void handleRobber() {
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (!sensor::tileChanged(t) || !sensor::tilePresent(t)) continue;
        if (t == game::robberTile()) continue;
        uint8_t old = game::robberTile();
        if (old < TILE_COUNT) led::undimTile(old);
        game::setRobberTile(t);
        led::dimTile(t);
        led::show();
        Serial.print(F("[ROBBER] Placed on tile "));
        Serial.println(t);
        game::setPhase(GamePhase::PLAYING);
        return;
    }

    if (pending_current_action == catan_PlayerAction_ACTION_SKIP_ROBBER) {
        pending_current_action = catan_PlayerAction_ACTION_NONE;
        Serial.println(F("[ROBBER] Skipped"));
        game::setPhase(GamePhase::PLAYING);
    }
}

// =============================================================================
// Phase: GAME_OVER
// =============================================================================

static void handleGameOver() {
    static bool announced = false;
    if (!announced) {
        uint8_t winner = game::checkWinner();
        Serial.print(F("[WINNER] P"));
        Serial.println(winner + 1);
        for (uint8_t i = 0; i < 5; ++i) {
            led::setAllTiles(kPlayerColors[winner % MAX_PLAYERS]);
            led::show();
            delay(300);
            led::setAllTiles(CRGB::Black);
            led::show();
            delay(300);
        }
        led::colorAllTilesByBiome();
        led::show();
        announced = true;
    }
}
