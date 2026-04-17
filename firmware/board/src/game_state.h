#pragma once
// =============================================================================
// game_state.h — Core Catan game logic and state tracking.
//
// Tracks: game phase, turns, resources, victory points, ownership,
// robber, dice, number reveal, and initial-placement rounds.
// =============================================================================

#include <stdint.h>
#include "config.h"
#include "board_types.h"

// ── Game phases (mirrored in catan.proto GamePhase enum) ────────────────────
enum class GamePhase : uint8_t {
    WAITING_FOR_PLAYERS,
    BOARD_SETUP,
    NUMBER_REVEAL,
    INITIAL_PLACEMENT,
    PLAYING,
    ROBBER,
    TRADE,
    GAME_OVER
};

// ── Ownership ───────────────────────────────────────────────────────────────
static constexpr uint8_t NO_PLAYER = 0xFF;

struct VertexState {
    uint8_t owner;
    bool    is_city;
};

struct EdgeState {
    uint8_t owner;
};

// ── Player data ─────────────────────────────────────────────────────────────
struct PlayerData {
    uint8_t  resources[NUM_RESOURCES];  // lumber, wool, grain, brick, ore
    uint8_t  victory_points;
    bool     connected;
};

// ── Public interface ────────────────────────────────────────────────────────
namespace game {

void init();

// Phase
GamePhase phase();
void      setPhase(GamePhase p);

// Players
uint8_t numPlayers();
void    setNumPlayers(uint8_t n);
bool    playerConnected(uint8_t id);
void    setPlayerConnected(uint8_t id, bool connected);

// Turn tracking
uint8_t currentPlayer();
void    nextTurn();

// Resources
uint8_t playerResource(uint8_t player, uint8_t res_idx);
void    addResource(uint8_t player, uint8_t res_idx, uint8_t amount);
bool    removeResource(uint8_t player, uint8_t res_idx, uint8_t amount);
uint8_t totalResources(uint8_t player);
void    distributeResources(uint8_t dice_total);  // Give resources for rolled number

// Victory points
uint8_t victoryPoints(uint8_t player);
void    recalcVictoryPoints(uint8_t player);
uint8_t checkWinner();  // Returns player ID or NO_PLAYER

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

// Initial placement
uint8_t setupRound();          // Current round (1 or 2)
bool    advanceSetupRound();   // Returns false when all rounds complete
void    resetSetupRound();
uint8_t setupPlacementsLeft(uint8_t player);  // How many placements remain this round

// Dice
uint8_t lastDie1();
uint8_t lastDie2();
uint8_t lastDiceTotal();
void    rollDice();
bool    hasRolled();
void    setHasRolled(bool rolled);

// Trade — port-based bank trade
bool canPortTrade(uint8_t player, uint8_t give_res, uint8_t get_res);
void doPortTrade(uint8_t player, uint8_t give_res, uint8_t get_res);

// Get player data struct for proto serialization
const PlayerData& playerData(uint8_t player);

}  // namespace game
