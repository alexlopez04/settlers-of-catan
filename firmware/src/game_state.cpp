// =============================================================================
// game_state.cpp — Catan game logic and state tracking.
// =============================================================================

#include "game_state.h"
#include "dice.h"
#include <string.h>

namespace {

GamePhase    current_phase    = GamePhase::PLAYER_SELECT;
uint8_t      num_players      = MIN_PLAYERS;
uint8_t      current_player_  = 0;

VertexState  vertices[VERTEX_COUNT];
EdgeState    edges[EDGE_COUNT];

uint8_t      robber_tile      = 0xFF;  // No robber placed yet

// Number reveal state: walk through 2,3,4,5,6,8,9,10,11,12
static const uint8_t kRevealOrder[] = { 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };
static constexpr uint8_t REVEAL_COUNT = sizeof(kRevealOrder);
uint8_t reveal_index = 0;

// Last dice roll
uint8_t die1_ = 0, die2_ = 0;

}  // anonymous namespace

namespace game {

void init() {
    current_phase   = GamePhase::PLAYER_SELECT;
    num_players     = MIN_PLAYERS;
    current_player_ = 0;
    reveal_index    = 0;
    robber_tile     = 0xFF;
    die1_ = die2_  = 0;

    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) {
        vertices[i].owner   = NO_PLAYER;
        vertices[i].is_city = false;
    }
    for (uint8_t i = 0; i < EDGE_COUNT; ++i) {
        edges[i].owner = NO_PLAYER;
    }
}

GamePhase phase()               { return current_phase; }
void setPhase(GamePhase p)      { current_phase = p; }

uint8_t numPlayers()            { return num_players; }
void setNumPlayers(uint8_t n)   { if (n >= MIN_PLAYERS && n <= MAX_PLAYERS) num_players = n; }

uint8_t currentPlayer()         { return current_player_; }
void nextTurn()                 { current_player_ = (current_player_ + 1) % num_players; }

// ── Ownership ───────────────────────────────────────────────────────────────

const VertexState& vertexState(uint8_t vertex_id) {
    static const VertexState empty = { NO_PLAYER, false };
    return (vertex_id < VERTEX_COUNT) ? vertices[vertex_id] : empty;
}

const EdgeState& edgeState(uint8_t edge_id) {
    static const EdgeState empty = { NO_PLAYER };
    return (edge_id < EDGE_COUNT) ? edges[edge_id] : empty;
}

void placeSettlement(uint8_t vertex_id, uint8_t player) {
    if (vertex_id >= VERTEX_COUNT) return;
    vertices[vertex_id].owner   = player;
    vertices[vertex_id].is_city = false;
}

void upgradeToCity(uint8_t vertex_id) {
    if (vertex_id >= VERTEX_COUNT) return;
    if (vertices[vertex_id].owner != NO_PLAYER) {
        vertices[vertex_id].is_city = true;
    }
}

void placeRoad(uint8_t edge_id, uint8_t player) {
    if (edge_id >= EDGE_COUNT) return;
    edges[edge_id].owner = player;
}

// ── Robber ──────────────────────────────────────────────────────────────────

uint8_t robberTile()              { return robber_tile; }
void setRobberTile(uint8_t tid)   { if (tid < TILE_COUNT) robber_tile = tid; }

// ── Number reveal ───────────────────────────────────────────────────────────

uint8_t currentRevealNumber() {
    return (reveal_index < REVEAL_COUNT) ? kRevealOrder[reveal_index] : 0;
}

bool advanceReveal() {
    if (reveal_index < REVEAL_COUNT - 1) {
        ++reveal_index;
        return true;
    }
    return false;  // Finished
}

void resetReveal() { reveal_index = 0; }

// ── Dice ────────────────────────────────────────────────────────────────────

uint8_t lastDie1()       { return die1_; }
uint8_t lastDie2()       { return die2_; }
uint8_t lastDiceTotal()  { return die1_ + die2_; }

void rollDice() {
    dice::roll(die1_, die2_);
}

}  // namespace game
