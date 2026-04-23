// =============================================================================
// game_state.cpp — Simplified Catan game logic (no resource tracking).
// =============================================================================

#include "game_state.h"
#include "board_topology.h"
#include "dice.h"
#include <string.h>

namespace {

GamePhase    current_phase    = GamePhase::LOBBY;
uint8_t      num_players      = 0;
uint8_t      current_player_  = 0;

bool         player_connected_[MAX_PLAYERS] = {};
uint8_t      reported_vp_[MAX_PLAYERS]      = {};

VertexState  vertices[VERTEX_COUNT];
EdgeState    edges[EDGE_COUNT];

uint8_t      robber_tile_ = 0xFF;
uint8_t      winner_id_   = NO_PLAYER;

// Number reveal order (Catan standard: 2,3,…,6,8,…,12)
static const uint8_t kRevealOrder[] = { 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };
static constexpr uint8_t REVEAL_COUNT = sizeof(kRevealOrder);
uint8_t reveal_index = 0;

// Dice
uint8_t die1_ = 0, die2_ = 0;
bool    has_rolled_ = false;

// Initial placement (snake draft)
uint8_t setup_round_        = 0;
uint8_t setup_turn_         = 0;
uint8_t setup_first_player_ = 0;

}  // anonymous namespace

namespace game {

void init() {
    current_phase   = GamePhase::LOBBY;
    num_players     = 0;
    current_player_ = 0;
    reveal_index    = 0;
    robber_tile_    = 0xFF;
    winner_id_      = NO_PLAYER;
    die1_ = die2_   = 0;
    has_rolled_     = false;
    setup_round_        = 0;
    setup_turn_         = 0;
    setup_first_player_ = 0;

    memset(player_connected_, 0, sizeof(player_connected_));
    memset(reported_vp_,      0, sizeof(reported_vp_));

    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) {
        vertices[i].owner   = NO_PLAYER;
        vertices[i].is_city = false;
    }
    for (uint8_t i = 0; i < EDGE_COUNT; ++i) {
        edges[i].owner = NO_PLAYER;
    }
}

// ── Phase ───────────────────────────────────────────────────────────────────
GamePhase phase()          { return current_phase; }
void setPhase(GamePhase p) { current_phase = p; }

const char* phaseName(GamePhase p) {
    switch (p) {
        case GamePhase::LOBBY:             return "LOBBY";
        case GamePhase::BOARD_SETUP:       return "BOARD_SETUP";
        case GamePhase::NUMBER_REVEAL:     return "NUMBER_REVEAL";
        case GamePhase::INITIAL_PLACEMENT: return "INITIAL_PLACEMENT";
        case GamePhase::PLAYING:           return "PLAYING";
        case GamePhase::ROBBER:            return "ROBBER";
        case GamePhase::GAME_OVER:         return "GAME_OVER";
    }
    return "?";
}

// ── Players ─────────────────────────────────────────────────────────────────
uint8_t numPlayers() { return num_players; }
void setNumPlayers(uint8_t n) {
    if (n <= MAX_PLAYERS) num_players = n;
}
bool playerConnected(uint8_t id) {
    return (id < MAX_PLAYERS) ? player_connected_[id] : false;
}
void setPlayerConnected(uint8_t id, bool c) {
    if (id < MAX_PLAYERS) player_connected_[id] = c;
}
uint8_t connectedMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (player_connected_[i]) m |= (1 << i);
    return m;
}

// ── Turn ────────────────────────────────────────────────────────────────────
uint8_t currentPlayer() { return current_player_; }void setCurrentPlayer(uint8_t id) { if (id < MAX_PLAYERS) current_player_ = id; }void nextTurn() {
    has_rolled_ = false;
    die1_ = die2_ = 0;
    if (num_players == 0) return;
    current_player_ = (current_player_ + 1) % num_players;
}

// ── Self-reported VP ────────────────────────────────────────────────────────
uint8_t reportedVp(uint8_t p) {
    return (p < MAX_PLAYERS) ? reported_vp_[p] : 0;
}
void setReportedVp(uint8_t p, uint8_t vp) {
    if (p < MAX_PLAYERS) reported_vp_[p] = vp;
}
uint8_t checkWinner() {
    for (uint8_t p = 0; p < num_players; ++p) {
        if (reported_vp_[p] >= VP_TO_WIN) return p;
    }
    return NO_PLAYER;
}

// ── Ownership ───────────────────────────────────────────────────────────────
const VertexState& vertexState(uint8_t v) {
    static const VertexState empty = { NO_PLAYER, false };
    return (v < VERTEX_COUNT) ? vertices[v] : empty;
}
const EdgeState& edgeState(uint8_t e) {
    static const EdgeState empty = { NO_PLAYER };
    return (e < EDGE_COUNT) ? edges[e] : empty;
}
void placeSettlement(uint8_t v, uint8_t p) {
    if (v >= VERTEX_COUNT || p >= MAX_PLAYERS) return;
    vertices[v].owner   = p;
    vertices[v].is_city = false;
}
void upgradeToCity(uint8_t v) {
    if (v >= VERTEX_COUNT) return;
    if (vertices[v].owner != NO_PLAYER) vertices[v].is_city = true;
}
void placeRoad(uint8_t e, uint8_t p) {
    if (e >= EDGE_COUNT || p >= MAX_PLAYERS) return;
    edges[e].owner = p;
}

// ── Robber ──────────────────────────────────────────────────────────────────
uint8_t robberTile() { return robber_tile_; }
void setRobberTile(uint8_t t) { if (t < TILE_COUNT) robber_tile_ = t; }

// ── Number reveal ───────────────────────────────────────────────────────────
uint8_t currentRevealNumber() {
    return (reveal_index < REVEAL_COUNT) ? kRevealOrder[reveal_index] : 0;
}
bool advanceReveal() {
    if (reveal_index < REVEAL_COUNT - 1) { ++reveal_index; return true; }
    return false;
}
void resetReveal() { reveal_index = 0; }

// ── Initial placement — snake draft ───────────────────────────────────────────────────────
// Snake order: N players starting at `first`.
//   Turn i < N  : player = (first + i)         % N   (forward)
//   Turn i >= N : player = (first + 2N-1-i)    % N   (reverse)
// The junction (turn N-1 and turn N) is the same player going twice.
static uint8_t playerForTurn(uint8_t i, uint8_t n, uint8_t first) {
    if (n == 0) return 0;
    if (i < n) return (first + i) % n;
    return (uint8_t)((first + (uint8_t)(2 * n - 1u - i)) % n);
}

uint8_t setupRound()       { return setup_round_; }
uint8_t setupTurn()        { return setup_turn_; }
uint8_t setupFirstPlayer() { return setup_first_player_; }

void resetSetupRound(uint8_t first_player) {
    if (num_players == 0) return;
    setup_first_player_ = first_player % num_players;
    setup_turn_         = 0;
    setup_round_        = 1;
    current_player_     = playerForTurn(0, num_players, setup_first_player_);
}

bool advanceSetupTurn() {
    uint8_t total = (uint8_t)(2u * num_players);
    if (setup_turn_ + 1u >= total) return false;   // all turns exhausted
    ++setup_turn_;
    setup_round_    = (setup_turn_ < num_players) ? 1 : 2;
    current_player_ = playerForTurn(setup_turn_, num_players, setup_first_player_);
    return true;
}

// ── Dice ───────────────────────────────────────────────────────────────────
uint8_t lastDie1()      { return die1_; }
uint8_t lastDie2()      { return die2_; }
uint8_t lastDiceTotal() { return die1_ + die2_; }
bool    hasRolled()     { return has_rolled_; }
void    setHasRolled(bool r) { has_rolled_ = r; }
void    clearDice() { die1_ = die2_ = 0; has_rolled_ = false; }
void    rollDice() {
    dice::roll(die1_, die2_);
    has_rolled_ = true;
}

}  // namespace game
