#pragma once
// =============================================================================
// game_state.h — Core Catan game logic and state tracking.
// =============================================================================

#include <stdint.h>
#include "config.h"

// ── Game phases ─────────────────────────────────────────────────────────────
enum class GamePhase : uint8_t {
    PLAYER_SELECT,     // Choose number of players
    WAIT_TO_START,     // Any button starts setup
    BOARD_SETUP,       // Auto-assign biomes & numbers, show LEDs
    NUMBER_REVEAL,     // Step through numbers one at a time
    PLAYING,           // Main gameplay loop
    GAME_OVER
};

// ── Ownership ───────────────────────────────────────────────────────────────
static constexpr uint8_t NO_PLAYER = 0xFF;

struct VertexState {
    uint8_t owner;     // Player number (0-based) or NO_PLAYER
    bool    is_city;   // false = settlement, true = city (if owned)
};

struct EdgeState {
    uint8_t owner;     // Player number (0-based) or NO_PLAYER
};

// ── Public interface ────────────────────────────────────────────────────────
namespace game {

void init();

GamePhase phase();
void setPhase(GamePhase p);

// ── Player Select ───────────────────────────────────────────────────────
uint8_t numPlayers();
void    setNumPlayers(uint8_t n);

// ── Turn tracking ───────────────────────────────────────────────────────
uint8_t currentPlayer();     // 0-based index
void    nextTurn();

// ── Ownership ───────────────────────────────────────────────────────────
const VertexState& vertexState(uint8_t vertex_id);
const EdgeState&   edgeState(uint8_t edge_id);

void placeSettlement(uint8_t vertex_id, uint8_t player);
void upgradeToCity(uint8_t vertex_id);
void placeRoad(uint8_t edge_id, uint8_t player);

// ── Robber ──────────────────────────────────────────────────────────────
uint8_t robberTile();            // Current tile holding the robber
void    setRobberTile(uint8_t tile_id);

// ── Number reveal ───────────────────────────────────────────────────────
uint8_t currentRevealNumber();   // The number being shown (2–12)
bool    advanceReveal();         // Move to next number. Returns false if done.
void    resetReveal();

// ── Dice ────────────────────────────────────────────────────────────────
uint8_t lastDie1();
uint8_t lastDie2();
uint8_t lastDiceTotal();
void    rollDice();

}  // namespace game
