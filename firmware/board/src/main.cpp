// =============================================================================
// main.cpp — Electronic Settlers of Catan: Central Board Controller
//
// Arduino Mega — source of truth for all game state.
// Controls tile LEDs, monitors hall-effect sensors, and communicates with
// up to 4 ESP32 player stations via I2C using protobuf messages.
//
// Game phases:
//   1. WAITING_FOR_PLAYERS — detect ESP32 stations on I2C bus
//   2. BOARD_SETUP         — randomize biomes & numbers, light LEDs
//   3. NUMBER_REVEAL       — step through number tokens (player presses Next)
//   4. INITIAL_PLACEMENT   — 2 rounds: each player places settlement + road
//   5. PLAYING             — roll dice, collect resources, build, trade
//   6. ROBBER              — move robber when 7 is rolled
//   7. TRADE               — port/bank trading sub-phase
//   8. GAME_OVER           — winner declared
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "board_types.h"
#include "board_topology.h"
#include "led_manager.h"
#include "sensor_manager.h"
#include "game_state.h"
#include "dice.h"
#include "comm_manager.h"
#include "proto/catan.pb.h"

// ── Forward declarations ────────────────────────────────────────────────────
static void handleWaitingForPlayers();
static void handleBoardSetup();
static void handleNumberReveal();
static void handleInitialPlacement();
static void handlePlaying();
static void handleRobber();
static void handleTrade();
static void handleGameOver();
static catan_ButtonId pollPlayerButton(uint8_t player_id);
static catan_ButtonId pollAnyButton();

// ── State ───────────────────────────────────────────────────────────────────
static uint8_t  connected_mask   = 0;
static uint32_t last_detect_ms   = 0;
static uint32_t last_sync_ms     = 0;

// Trade sub-phase state
static uint8_t  trade_give_res   = 0;
static uint8_t  trade_get_res    = 1;

// Player colors for LED feedback
static const CRGB kPlayerColors[MAX_PLAYERS] = {
    CRGB::Red, CRGB::Blue, CRGB::Orange, CRGB::White
};

// Port color helper
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

// =============================================================================
// setup()
// =============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { ; }

    Serial.println(F("=== Settlers of Catan — Central Board ==="));

    Wire.begin();
    Wire.setClock(100000);

    led::init();
    sensor::init();
    dice::init(analogRead(A0));
    comm::init();
    game::init();

    led::setAllTiles(CRGB(20, 20, 40));  // Dim blue on startup
    led::show();

    game::setPhase(GamePhase::WAITING_FOR_PLAYERS);
    Serial.println(F("[READY] Waiting for player stations..."));
}

// =============================================================================
// loop()
// =============================================================================

void loop() {
    sensor::poll();

    switch (game::phase()) {
        case GamePhase::WAITING_FOR_PLAYERS: handleWaitingForPlayers(); break;
        case GamePhase::BOARD_SETUP:         handleBoardSetup();        break;
        case GamePhase::NUMBER_REVEAL:       handleNumberReveal();      break;
        case GamePhase::INITIAL_PLACEMENT:   handleInitialPlacement();  break;
        case GamePhase::PLAYING:             handlePlaying();           break;
        case GamePhase::ROBBER:              handleRobber();            break;
        case GamePhase::TRADE:               handleTrade();             break;
        case GamePhase::GAME_OVER:           handleGameOver();          break;
    }

    // Periodic state sync to all connected players
    if (millis() - last_sync_ms >= COMM_INTERVAL_MS) {
        last_sync_ms = millis();
        comm::syncStateToAll(connected_mask);
    }

    delay(SENSOR_POLL_MS);
}

// =============================================================================
// Helper: Poll a specific player's button input
// =============================================================================

static catan_ButtonId pollPlayerButton(uint8_t player_id) {
    if (!(connected_mask & (1 << player_id))) return catan_ButtonId_BTN_NONE;

    catan_PlayerToBoard resp = catan_PlayerToBoard_init_zero;
    if (comm::readFromPlayer(player_id, resp)) {
        if (resp.type == catan_MsgType_MSG_BUTTON_EVENT) {
            return resp.button;
        }
    }
    return catan_ButtonId_BTN_NONE;
}

// =============================================================================
// Helper: Poll ALL connected players, return first button press found
// =============================================================================

static catan_ButtonId pollAnyButton() {
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        catan_ButtonId btn = pollPlayerButton(i);
        if (btn != catan_ButtonId_BTN_NONE) return btn;
    }
    return catan_ButtonId_BTN_NONE;
}

// =============================================================================
// Phase: WAITING_FOR_PLAYERS
// =============================================================================

static void handleWaitingForPlayers() {
    if (millis() - last_detect_ms >= PLAYER_DETECT_MS) {
        last_detect_ms = millis();
        uint8_t new_mask = comm::detectPlayers();

        if (new_mask != connected_mask) {
            connected_mask = new_mask;

            uint8_t count = 0;
            for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                if (connected_mask & (1 << i)) {
                    game::setPlayerConnected(i, true);
                    count++;
                    Serial.print(F("[DETECT] Player "));
                    Serial.print(i + 1);
                    Serial.println(F(" connected"));
                } else {
                    game::setPlayerConnected(i, false);
                }
            }
            game::setNumPlayers(count);
            Serial.print(F("[DETECT] Total: "));
            Serial.println(count);

            // Visual feedback
            led::setAllTiles(CRGB(20, 20, 40));
            for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                if (connected_mask & (1 << i)) {
                    led::setTileColor(i, kPlayerColors[i]);
                }
            }
            led::show();
        }
    }

    if (game::numPlayers() >= MIN_PLAYERS) {
        catan_ButtonId btn = pollAnyButton();
        if (btn != catan_ButtonId_BTN_NONE) {
            Serial.print(F("[START] Beginning with "));
            Serial.print(game::numPlayers());
            Serial.println(F(" players"));
            game::setPhase(GamePhase::BOARD_SETUP);
        }
    }
}

// =============================================================================
// Phase: BOARD_SETUP
// =============================================================================

static void handleBoardSetup() {
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

    game::resetReveal();
    game::setPhase(GamePhase::NUMBER_REVEAL);
    Serial.println(F("[SETUP] Board ready — number reveal"));
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
            if (g_tile_state[t].number == num) {
                led::setTileColor(t, CRGB::White);
            }
        }
        led::show();

        Serial.print(F("[REVEAL] Number "));
        Serial.println(num);
    }

    catan_ButtonId btn = pollAnyButton();
    if (btn == catan_ButtonId_BTN_CENTER || btn == catan_ButtonId_BTN_RIGHT) {
        if (!game::advanceReveal()) {
            last_shown = 0;
            led::colorAllTilesByBiome();
            if (game::robberTile() < TILE_COUNT) {
                led::dimTile(game::robberTile());
            }
            led::show();

            game::resetSetupRound();
            game::setPhase(GamePhase::INITIAL_PLACEMENT);
            Serial.println(F("[GAME] Entering initial placement"));
        }
    }
}

// =============================================================================
// Phase: INITIAL_PLACEMENT
// =============================================================================

static void handleInitialPlacement() {
    uint8_t cp = game::currentPlayer();

    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (!sensor::vertexChanged(v)) continue;
        if (!sensor::vertexPresent(v)) continue;

        if (game::vertexState(v).owner == NO_PLAYER) {
            game::placeSettlement(v, cp);
            Serial.print(F("[SETUP] P"));
            Serial.print(cp + 1);
            Serial.print(F(" settlement v"));
            Serial.println(v);

            uint8_t adj[3];
            uint8_t cnt = tilesForVertex(v, adj, 3);
            if (cnt > 0) {
                led::flashTiles(adj, cnt, kPlayerColors[cp % MAX_PLAYERS], 2, 150);
            }
        }
    }

    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (!sensor::edgeChanged(e)) continue;
        if (!sensor::edgePresent(e)) continue;

        if (game::edgeState(e).owner == NO_PLAYER) {
            game::placeRoad(e, cp);
            Serial.print(F("[SETUP] P"));
            Serial.print(cp + 1);
            Serial.print(F(" road e"));
            Serial.println(e);
        }
    }

    catan_ButtonId btn = pollPlayerButton(cp);
    if (btn == catan_ButtonId_BTN_CENTER) {
        Serial.print(F("[SETUP] P"));
        Serial.print(cp + 1);
        Serial.println(F(" confirmed"));

        uint8_t next = (game::currentPlayer() + 1) % game::numPlayers();
        if (next == 0) {
            if (!game::advanceSetupRound()) {
                game::setPhase(GamePhase::PLAYING);
                game::setHasRolled(false);
                Serial.println(F("[GAME] Setup done — play phase"));
            } else {
                Serial.print(F("[SETUP] Round "));
                Serial.println(game::setupRound());
            }
        }
        game::nextTurn();
        if (game::phase() == GamePhase::PLAYING) {
            game::setHasRolled(false);
        }
    }
}

// =============================================================================
// Phase: PLAYING
// =============================================================================

static void handlePlaying() {
    uint8_t cp = game::currentPlayer();
    catan_ButtonId btn = pollPlayerButton(cp);

    // Roll dice
    if (btn == catan_ButtonId_BTN_LEFT && !game::hasRolled()) {
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
        if (match_count > 0) {
            led::flashTiles(matching, match_count, CRGB::White, 3, 200);
        }

        if (total == ROBBER_ROLL) {
            Serial.println(F("[ROBBER] Seven rolled!"));
            game::setPhase(GamePhase::ROBBER);
            return;
        }

        game::distributeResources(total);
    }

    // Trade
    if (btn == catan_ButtonId_BTN_LEFT && game::hasRolled()) {
        trade_give_res = 0;
        trade_get_res  = 1;
        game::setPhase(GamePhase::TRADE);
        return;
    }

    // End turn
    if (btn == catan_ButtonId_BTN_RIGHT && game::hasRolled()) {
        Serial.print(F("[TURN] P"));
        Serial.print(cp + 1);
        Serial.println(F(" ends turn"));
        game::nextTurn();
        Serial.print(F("[TURN] Now P"));
        Serial.println(game::currentPlayer() + 1);
    }

    // Piece detection
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
            if (cnt > 0) {
                led::flashTiles(adj, cnt, kPlayerColors[cp % MAX_PLAYERS], 2, 150);
            }
        } else if (vs.owner == cp && !vs.is_city) {
            game::upgradeToCity(v);
            Serial.print(F("[BUILD] P"));
            Serial.print(cp + 1);
            Serial.print(F(" city v"));
            Serial.println(v);
        }

        if (game::checkWinner() != NO_PLAYER) {
            game::setPhase(GamePhase::GAME_OVER);
            return;
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

            uint8_t adj[2];
            uint8_t cnt = tilesForEdge(e, adj, 2);
            if (cnt > 0) {
                led::flashTiles(adj, cnt, kPlayerColors[cp % MAX_PLAYERS], 2, 150);
            }
        }
    }

    // Robber movement via tile sensor (manual outside of 7 roll)
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

    catan_ButtonId btn = pollPlayerButton(game::currentPlayer());
    if (btn == catan_ButtonId_BTN_CENTER) {
        Serial.println(F("[ROBBER] Skipped"));
        game::setPhase(GamePhase::PLAYING);
    }
}

// =============================================================================
// Phase: TRADE
// =============================================================================

static void handleTrade() {
    uint8_t cp = game::currentPlayer();
    catan_ButtonId btn = pollPlayerButton(cp);

    if (btn == catan_ButtonId_BTN_LEFT) {
        trade_give_res = (trade_give_res + 1) % NUM_RESOURCES;
        if (trade_give_res == trade_get_res)
            trade_give_res = (trade_give_res + 1) % NUM_RESOURCES;
    }

    if (btn == catan_ButtonId_BTN_RIGHT) {
        trade_get_res = (trade_get_res + 1) % NUM_RESOURCES;
        if (trade_get_res == trade_give_res)
            trade_get_res = (trade_get_res + 1) % NUM_RESOURCES;
    }

    if (btn == catan_ButtonId_BTN_CENTER) {
        if (game::canPortTrade(cp, trade_give_res, trade_get_res)) {
            game::doPortTrade(cp, trade_give_res, trade_get_res);
            Serial.print(F("[TRADE] P"));
            Serial.print(cp + 1);
            Serial.println(F(" traded"));
        } else {
            game::setPhase(GamePhase::PLAYING);
        }
    }
}

// =============================================================================
// Phase: GAME_OVER
// =============================================================================

static void handleGameOver() {
    static bool announced = false;
    if (!announced) {
        uint8_t winner = game::checkWinner();
        Serial.print(F("[WINNER] Player "));
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