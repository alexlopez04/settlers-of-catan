#pragma once
// =============================================================================
// game_state.h — Simplified Catan game logic.
//
// The central board is now the authority on:
//   - game phase / turn
//   - tile layout (biomes + numbers) and the robber
//   - piece placement (settlement / city / road ownership)
//   - dice rolls and the number-reveal order
//
// Victory points and resources are tracked by the mobile app and reported
// back to the board via PlayerInput(ACTION_REPORT).  The board does NOT
// compute resource distribution — it only records the self-reported VP
// values to broadcast them to other players.
// =============================================================================

#include <stdint.h>
#include "config.h"
#include "board_types.h"

enum class GamePhase : uint8_t {
    LOBBY,
    BOARD_SETUP,
    NUMBER_REVEAL,
    INITIAL_PLACEMENT,
    PLAYING,
    ROBBER,
    GAME_OVER
};

static constexpr uint8_t NO_PLAYER = 0xFF;

struct VertexState { uint8_t owner; bool is_city; };
struct EdgeState   { uint8_t owner; };

namespace game {

void init();

// Phase
GamePhase phase();
void      setPhase(GamePhase p);
const char* phaseName(GamePhase p);

// Players
uint8_t numPlayers();
void    setNumPlayers(uint8_t n);
bool    playerConnected(uint8_t id);
void    setPlayerConnected(uint8_t id, bool connected);
uint8_t connectedMask();

// Turn
uint8_t currentPlayer();
void    setCurrentPlayer(uint8_t id);
void    nextTurn();

// Self-reported VP (via PlayerInput ACTION_REPORT)
uint8_t reportedVp(uint8_t player);
void    setReportedVp(uint8_t player, uint8_t vp);
uint8_t checkWinner();                // returns player id or NO_PLAYER

// Ownership
const VertexState& vertexState(uint8_t vertex_id);
const EdgeState&   edgeState(uint8_t edge_id);
void placeSettlement(uint8_t vertex_id, uint8_t player);
void upgradeToCity(uint8_t vertex_id);
void placeRoad(uint8_t edge_id, uint8_t player);

// Robber
uint8_t robberTile();
void    setRobberTile(uint8_t tile_id);

// Number reveal
uint8_t currentRevealNumber();
bool    advanceReveal();
void    resetReveal();

// Initial placement — snake draft
// resetSetupRound(first) builds the snake sequence starting from `first`.
// advanceSetupTurn() steps to the next slot; returns false when all 2*N turns
// are complete (i.e. the caller should enter PLAYING).
uint8_t setupRound();        // 1 = forward, 2 = reverse
uint8_t setupTurn();         // 0-based position in the 2*N sequence
uint8_t setupFirstPlayer();  // the randomly chosen starting player
void    resetSetupRound(uint8_t first_player);
bool    advanceSetupTurn();

// Dice
uint8_t lastDie1();
uint8_t lastDie2();
uint8_t lastDiceTotal();
void    rollDice();
bool    hasRolled();
void    setHasRolled(bool rolled);
void    clearDice();

}  // namespace game
