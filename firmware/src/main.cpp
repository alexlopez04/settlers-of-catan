// =============================================================================
// main.cpp — Electronic Settlers of Catan Board Controller
//
// Gameplay phases:
//   1. PLAYER_SELECT  — Left=-, Right=+, Center=confirm player count
//   2. WAIT_TO_START  — any button starts board setup
//   3. BOARD_SETUP    — auto-randomizes and transitions immediately
//   4. NUMBER_REVEAL  — Center=Next to step through each number token
//   5. PLAYING        — Left=Roll dice, Right=Next turn
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "board_layout.h"
#include "led_manager.h"
#include "sensor_manager.h"
#include "button_manager.h"
#include "display_manager.h"
#include "game_state.h"
#include "dice.h"

// ── Forward declarations for phase handlers ─────────────────────────────────
static void handlePlayerSelect();
static void handleWaitToStart();
static void handleBoardSetup();
static void handleNumberReveal();
static void handlePlaying();

// ── Port color helper ───────────────────────────────────────────────────────
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

// ── Player colors ───────────────────────────────────────────────────────────
static const CRGB kPlayerColors[MAX_PLAYERS] = {
    CRGB::Red, CRGB::Blue, CRGB::Orange, CRGB::White
};

// =============================================================================
// setup()
// =============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { ; }

    Serial.println(F("=== Settlers of Catan Board ==="));

    Wire.begin();  // I2C master for expanders + LCD

    led::init();
    sensor::init();
    button::init();
    display::init();
    dice::init(analogRead(A0));  // Seed from floating analog pin

    game::init();
    game::setPhase(GamePhase::PLAYER_SELECT);

    led::setAllTiles(CRGB(40, 40, 40));  // Dim white on startup
    led::show();

    display::clear();
    display::printRow(0, "Select Players");
    char buf[22];
    snprintf(buf, sizeof(buf), "Players: %u", game::numPlayers());
    display::printRow(2, buf);
    display::showButtonBar("-", "Next", "+");

    Serial.println(F("[READY] Awaiting player count"));
}

// =============================================================================
// loop()
// =============================================================================

void loop() {
    button::poll();
    sensor::poll();

    switch (game::phase()) {
        case GamePhase::PLAYER_SELECT:  handlePlayerSelect(); break;
        case GamePhase::WAIT_TO_START:  handleWaitToStart();  break;
        case GamePhase::BOARD_SETUP:    handleBoardSetup();   break;
        case GamePhase::NUMBER_REVEAL:  handleNumberReveal(); break;
        case GamePhase::PLAYING:        handlePlaying();      break;
        case GamePhase::GAME_OVER:      break;
    }

    delay(SENSOR_POLL_MS);
}

// =============================================================================
// Phase: PLAYER_SELECT
// Left=decrease, Right=increase, Center=confirm.
// =============================================================================

static void handlePlayerSelect() {
    bool changed = false;

    if (button::leftPressed()) {
        uint8_t n = game::numPlayers() - 1;
        if (n < MIN_PLAYERS) n = MAX_PLAYERS;
        game::setNumPlayers(n);
        changed = true;
    }

    if (button::rightPressed()) {
        uint8_t n = game::numPlayers() + 1;
        if (n > MAX_PLAYERS) n = MIN_PLAYERS;
        game::setNumPlayers(n);
        changed = true;
    }

    if (changed) {
        char buf[22];
        snprintf(buf, sizeof(buf), "Players: %u", game::numPlayers());
        display::printRow(2, buf);
        Serial.print(F("[SEL] Players="));
        Serial.println(game::numPlayers());
    }

    if (button::centerPressed()) {
        Serial.print(F("[SEL] Confirmed "));
        Serial.print(game::numPlayers());
        Serial.println(F(" players"));

        display::clear();
        display::printRow(0, "Ready to Start!");
        display::printRow(2, "Press any button");
        display::printRow(3, "to begin setup");
        display::showButtonBar("Start", "Start", "Start");
        game::setPhase(GamePhase::WAIT_TO_START);
    }
}

// =============================================================================
// Phase: WAIT_TO_START — any button triggers board setup.
// =============================================================================

static void handleWaitToStart() {
    if (button::leftPressed() || button::centerPressed() || button::rightPressed()) {
        Serial.println(F("[SETUP] Starting board setup"));
        game::setPhase(GamePhase::BOARD_SETUP);
    }
}

// =============================================================================
// Phase: BOARD_SETUP — randomize board and show biome colors + ports.
// =============================================================================

static void handleBoardSetup() {
    display::clear();
    display::showTitle("Setting up board...");

    randomizeBoardLayout();

    // Find the desert tile and place the robber there
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (g_tiles[t].biome == Biome::DESERT) {
            game::setRobberTile(t);
            break;
        }
    }

    // Light tiles by biome
    led::colorAllTilesByBiome();

    // Light ports
    for (uint8_t p = 0; p < PORT_COUNT; ++p) {
        led::setPortColor(p, portColor(g_ports[p].type));
    }
    led::show();

    // Log tile assignments
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        Serial.print(F("[TILE] "));
        Serial.print(t);
        Serial.print(F(" "));
        Serial.print(biomeName(g_tiles[t].biome));
        if (g_tiles[t].number > 0) {
            Serial.print(F(" #"));
            Serial.print(g_tiles[t].number);
        }
        Serial.println();
    }

    display::clear();
    display::printRow(0, "Board ready!");
    display::printRow(2, "Token reveal:");
    display::printRow(3, "step thru numbers");
    display::showButtonBar("", "Next", "");

    game::resetReveal();
    game::setPhase(GamePhase::NUMBER_REVEAL);
}

// =============================================================================
// Phase: NUMBER_REVEAL — step through each number 2–12 (excl. 7).
// Lights all tiles of the current number. Center button advances.
// =============================================================================

static void handleNumberReveal() {
    static uint8_t last_shown_number = 0;
    uint8_t num = game::currentRevealNumber();

    if (num != last_shown_number) {
        last_shown_number = num;

        // Turn off all tiles first
        led::setAllTiles(CRGB::Black);

        // Highlight only tiles that match this number
        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            if (g_tiles[t].number == num) {
                led::setTileColor(t, CRGB::White);
            }
        }
        led::show();

        char buf[22];
        snprintf(buf, sizeof(buf), "Number: %u", num);
        display::clear();
        display::printRow(0, "Number Reveal");
        display::printRow(2, buf);
        display::showButtonBar("", "Next", "");

        Serial.print(F("[REVEAL] Showing number "));
        Serial.println(num);
    }

    if (button::centerPressed()) {
        if (!game::advanceReveal()) {
            // All numbers revealed — move to gameplay
            last_shown_number = 0;
            led::colorAllTilesByBiome();
            if (game::robberTile() < TILE_COUNT) {
                led::dimTile(game::robberTile());
            }
            led::show();

            display::clear();
            display::showPlayerTurn(game::currentPlayer(), game::numPlayers());
            display::printRow(2, "Roll the dice!");
            display::showButtonBar("Roll", "", "Next");

            game::setPhase(GamePhase::PLAYING);
            Serial.println(F("[GAME] Entering play phase"));
        }
    }
}

// =============================================================================
// Phase: PLAYING — main game loop.
// Left=Roll dice, Right=Next turn.
// =============================================================================

static void handlePlaying() {
    // ── Dice roll ───────────────────────────────────────────────────────────
    if (button::leftPressed()) {
        game::rollDice();
        uint8_t d1 = game::lastDie1();
        uint8_t d2 = game::lastDie2();
        uint8_t total = game::lastDiceTotal();

        display::showDiceResult(d1, d2);
        Serial.print(F("[DICE] "));
        Serial.print(d1);
        Serial.print(F("+"));
        Serial.print(d2);
        Serial.print(F("="));
        Serial.println(total);

        // Flash tiles matching the rolled number
        uint8_t matching[TILE_COUNT];
        uint8_t match_count = 0;
        for (uint8_t t = 0; t < TILE_COUNT; ++t) {
            if (g_tiles[t].number == total && t != game::robberTile()) {
                matching[match_count++] = t;
            }
        }
        if (match_count > 0) {
            led::flashTiles(matching, match_count, CRGB::White, 3, 200);
        }
    }

    // ── Next turn ───────────────────────────────────────────────────────────
    if (button::rightPressed()) {
        game::nextTurn();
        display::clear();
        display::showPlayerTurn(game::currentPlayer(), game::numPlayers());
        display::printRow(2, "Roll the dice!");
        display::showButtonBar("Roll", "", "Next");
        Serial.print(F("[TURN] Player "));
        Serial.println(game::currentPlayer() + 1);
    }

    // ── Vertex sensor changes (settlement / city placement) ─────────────────
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (!sensor::vertexChanged(v)) continue;

        bool present = sensor::vertexPresent(v);
        uint8_t player = game::currentPlayer();

        if (present) {
            // New piece placed
            const VertexState& vs = game::vertexState(v);
            if (vs.owner == NO_PLAYER) {
                game::placeSettlement(v, player);
                Serial.print(F("[CITY] Settlement at v"));
                Serial.print(v);
                Serial.print(F(" by P"));
                Serial.println(player + 1);
            } else if (vs.owner == player && !vs.is_city) {
                game::upgradeToCity(v);
                Serial.print(F("[CITY] Upgraded to city at v"));
                Serial.println(v);
            }

            // Flash adjacent tiles
            uint8_t adj_tiles[3];
            uint8_t adj_count = tilesForVertex(v, adj_tiles, 3);
            if (adj_count > 0) {
                led::flashTiles(adj_tiles, adj_count,
                                kPlayerColors[player % MAX_PLAYERS], 3, 150);
            }
        } else {
            Serial.print(F("[CITY] Piece removed from v"));
            Serial.println(v);
        }
    }

    // ── Edge sensor changes (road placement) ────────────────────────────────
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (!sensor::edgeChanged(e)) continue;

        bool present = sensor::edgePresent(e);
        uint8_t player = game::currentPlayer();

        if (present) {
            const EdgeState& es = game::edgeState(e);
            if (es.owner == NO_PLAYER) {
                game::placeRoad(e, player);
                Serial.print(F("[ROAD] Road at e"));
                Serial.print(e);
                Serial.print(F(" by P"));
                Serial.println(player + 1);
            }

            // Flash adjacent tiles
            uint8_t adj_tiles[2];
            uint8_t adj_count = tilesForEdge(e, adj_tiles, 2);
            if (adj_count > 0) {
                led::flashTiles(adj_tiles, adj_count,
                                kPlayerColors[player % MAX_PLAYERS], 3, 150);
            }
        } else {
            Serial.print(F("[ROAD] Piece removed from e"));
            Serial.println(e);
        }
    }

    // ── Tile sensor changes (robber movement) ───────────────────────────────
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (!sensor::tileChanged(t)) continue;

        if (sensor::tilePresent(t)) {
            // Robber moved here
            uint8_t old_tile = game::robberTile();
            if (old_tile < TILE_COUNT) {
                led::undimTile(old_tile);
            }
            game::setRobberTile(t);
            led::dimTile(t);
            led::show();

            Serial.print(F("[ROBBER] Moved to tile "));
            Serial.println(t);

            char buf[22];
            snprintf(buf, sizeof(buf), "Robber->tile %u", t);
            display::printRow(3, buf);
            display::showButtonBar("Roll", "", "Next");
        }
    }
}
