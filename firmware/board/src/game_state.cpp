// =============================================================================
// game_state.cpp — Catan game logic: resources, VP, placement, trading.
// =============================================================================

#include "game_state.h"
#include "board_topology.h"
#include "dice.h"
#include <string.h>
#include <Arduino.h>

namespace {

GamePhase    current_phase   = GamePhase::WAITING_FOR_PLAYERS;
uint8_t      num_players     = 0;
uint8_t      current_player_ = 0;

PlayerData   players[MAX_PLAYERS];
VertexState  vertices[VERTEX_COUNT];
EdgeState    edges[EDGE_COUNT];

uint8_t      robber_tile     = 0xFF;

// Number reveal
static const uint8_t kRevealOrder[] = { 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };
static constexpr uint8_t REVEAL_COUNT = sizeof(kRevealOrder);
uint8_t reveal_index = 0;

// Dice
uint8_t die1_ = 0, die2_ = 0;
bool    has_rolled_ = false;

// Initial placement
uint8_t setup_round_       = 0;  // 0 = not started, 1 = first round, 2 = second
uint8_t setup_placements_[MAX_PLAYERS];  // settlements placed this round per player

}  // anonymous namespace

namespace game {

void init() {
    current_phase   = GamePhase::WAITING_FOR_PLAYERS;
    num_players     = 0;
    current_player_ = 0;
    reveal_index    = 0;
    robber_tile     = 0xFF;
    die1_ = die2_  = 0;
    has_rolled_     = false;
    setup_round_    = 0;

    memset(players, 0, sizeof(players));
    memset(setup_placements_, 0, sizeof(setup_placements_));

    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) {
        vertices[i].owner   = NO_PLAYER;
        vertices[i].is_city = false;
    }
    for (uint8_t i = 0; i < EDGE_COUNT; ++i) {
        edges[i].owner = NO_PLAYER;
    }
}

// ── Phase ───────────────────────────────────────────────────────────────────

GamePhase phase()            { return current_phase; }
void setPhase(GamePhase p)   { current_phase = p; }

// ── Players ─────────────────────────────────────────────────────────────────

uint8_t numPlayers()            { return num_players; }
void setNumPlayers(uint8_t n) {
    if (n >= MIN_PLAYERS && n <= MAX_PLAYERS) num_players = n;
}

bool playerConnected(uint8_t id) {
    return (id < MAX_PLAYERS) ? players[id].connected : false;
}

void setPlayerConnected(uint8_t id, bool connected) {
    if (id < MAX_PLAYERS) players[id].connected = connected;
}

// ── Turn ────────────────────────────────────────────────────────────────────

uint8_t currentPlayer()  { return current_player_; }

void nextTurn() {
    has_rolled_ = false;
    current_player_ = (current_player_ + 1) % num_players;
}

// ── Resources ───────────────────────────────────────────────────────────────

uint8_t playerResource(uint8_t player, uint8_t res_idx) {
    if (player >= MAX_PLAYERS || res_idx >= NUM_RESOURCES) return 0;
    return players[player].resources[res_idx];
}

void addResource(uint8_t player, uint8_t res_idx, uint8_t amount) {
    if (player >= MAX_PLAYERS || res_idx >= NUM_RESOURCES) return;
    players[player].resources[res_idx] += amount;
}

bool removeResource(uint8_t player, uint8_t res_idx, uint8_t amount) {
    if (player >= MAX_PLAYERS || res_idx >= NUM_RESOURCES) return false;
    if (players[player].resources[res_idx] < amount) return false;
    players[player].resources[res_idx] -= amount;
    return true;
}

uint8_t totalResources(uint8_t player) {
    if (player >= MAX_PLAYERS) return 0;
    uint8_t total = 0;
    for (uint8_t i = 0; i < NUM_RESOURCES; ++i)
        total += players[player].resources[i];
    return total;
}

void distributeResources(uint8_t dice_total) {
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (g_tile_state[t].number != dice_total) continue;
        if (t == robber_tile) continue;

        uint8_t res = biomeToResource(g_tile_state[t].biome);
        if (res == NONE) continue;

        // Check all vertices of this tile
        for (uint8_t vi = 0; vi < 6; ++vi) {
            uint8_t v = TILE_TOPO[t].vertices[vi];
            if (v == NONE) continue;
            if (vertices[v].owner == NO_PLAYER) continue;

            uint8_t amount = vertices[v].is_city ? 2 : 1;
            addResource(vertices[v].owner, res, amount);
        }
    }
}

// ── Victory Points ──────────────────────────────────────────────────────────

uint8_t victoryPoints(uint8_t player) {
    return (player < MAX_PLAYERS) ? players[player].victory_points : 0;
}

void recalcVictoryPoints(uint8_t player) {
    if (player >= MAX_PLAYERS) return;
    uint8_t vp = 0;
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        if (vertices[v].owner == player) {
            vp += vertices[v].is_city ? 2 : 1;
        }
    }
    players[player].victory_points = vp;
}

uint8_t checkWinner() {
    for (uint8_t p = 0; p < num_players; ++p) {
        if (players[p].victory_points >= VP_TO_WIN) return p;
    }
    return NO_PLAYER;
}

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
    if (vertex_id >= VERTEX_COUNT || player >= MAX_PLAYERS) return;
    vertices[vertex_id].owner   = player;
    vertices[vertex_id].is_city = false;
    recalcVictoryPoints(player);

    // Track for setup rounds
    if (setup_round_ > 0) {
        setup_placements_[player]++;

        // In round 2, give starting resources for second settlement
        if (setup_round_ == 2) {
            uint8_t adj_tiles[3];
            uint8_t count = tilesForVertex(vertex_id, adj_tiles, 3);
            for (uint8_t i = 0; i < count; ++i) {
                uint8_t res = biomeToResource(g_tile_state[adj_tiles[i]].biome);
                if (res != NONE) {
                    addResource(player, res, 1);
                }
            }
        }
    }
}

void upgradeToCity(uint8_t vertex_id) {
    if (vertex_id >= VERTEX_COUNT) return;
    if (vertices[vertex_id].owner != NO_PLAYER) {
        vertices[vertex_id].is_city = true;
        recalcVictoryPoints(vertices[vertex_id].owner);
    }
}

void placeRoad(uint8_t edge_id, uint8_t player) {
    if (edge_id >= EDGE_COUNT || player >= MAX_PLAYERS) return;
    edges[edge_id].owner = player;
}

// ── Robber ──────────────────────────────────────────────────────────────────

uint8_t robberTile()            { return robber_tile; }
void setRobberTile(uint8_t tid) { if (tid < TILE_COUNT) robber_tile = tid; }

// ── Number Reveal ───────────────────────────────────────────────────────────

uint8_t currentRevealNumber() {
    return (reveal_index < REVEAL_COUNT) ? kRevealOrder[reveal_index] : 0;
}

bool advanceReveal() {
    if (reveal_index < REVEAL_COUNT - 1) {
        ++reveal_index;
        return true;
    }
    return false;
}

void resetReveal() { reveal_index = 0; }

// ── Initial Placement ───────────────────────────────────────────────────────

uint8_t setupRound()       { return setup_round_; }

void resetSetupRound() {
    setup_round_ = 1;
    memset(setup_placements_, 0, sizeof(setup_placements_));
}

bool advanceSetupRound() {
    if (setup_round_ < INITIAL_ROUNDS) {
        setup_round_++;
        memset(setup_placements_, 0, sizeof(setup_placements_));
        return true;  // More rounds to go
    }
    setup_round_ = 0;
    return false;  // All rounds complete
}

uint8_t setupPlacementsLeft(uint8_t player) {
    if (player >= MAX_PLAYERS) return 0;
    // Each player places 1 settlement + 1 road per round
    return (setup_placements_[player] < 1) ? 1 : 0;
}

// ── Dice ────────────────────────────────────────────────────────────────────

uint8_t lastDie1()       { return die1_; }
uint8_t lastDie2()       { return die2_; }
uint8_t lastDiceTotal()  { return die1_ + die2_; }
bool    hasRolled()      { return has_rolled_; }
void    setHasRolled(bool r) { has_rolled_ = r; }

void rollDice() {
    dice::roll(die1_, die2_);
    has_rolled_ = true;
}

// ── Trade ───────────────────────────────────────────────────────────────────

static uint8_t tradeRatio(uint8_t player, uint8_t res_idx) {
    // Check if player has a 2:1 port for this resource
    for (uint8_t p = 0; p < PORT_COUNT; ++p) {
        const PortDef& port = PORT_TOPO[p];
        // Map port type to resource index
        uint8_t port_res = NONE;
        switch (port.type) {
            case PortType::LUMBER_2_1: port_res = 0; break;
            case PortType::WOOL_2_1:   port_res = 1; break;
            case PortType::GRAIN_2_1:  port_res = 2; break;
            case PortType::BRICK_2_1:  port_res = 3; break;
            case PortType::ORE_2_1:    port_res = 4; break;
            default: break;
        }

        if (port_res == res_idx) {
            // Check if player has a settlement/city on either port vertex
            for (uint8_t vi = 0; vi < 2; ++vi) {
                uint8_t v = port.vertices[vi];
                if (v < VERTEX_COUNT && vertices[v].owner == player) {
                    return 2;
                }
            }
        }

        // Check for 3:1 generic port
        if (port.type == PortType::GENERIC_3_1) {
            for (uint8_t vi = 0; vi < 2; ++vi) {
                uint8_t v = port.vertices[vi];
                if (v < VERTEX_COUNT && vertices[v].owner == player) {
                    if (3 < 4) return 3;  // 3:1 is better than default 4:1
                }
            }
        }
    }
    return 4;  // Default bank rate
}

bool canPortTrade(uint8_t player, uint8_t give_res, uint8_t get_res) {
    if (player >= MAX_PLAYERS) return false;
    if (give_res >= NUM_RESOURCES || get_res >= NUM_RESOURCES) return false;
    if (give_res == get_res) return false;
    uint8_t ratio = tradeRatio(player, give_res);
    return players[player].resources[give_res] >= ratio;
}

void doPortTrade(uint8_t player, uint8_t give_res, uint8_t get_res) {
    if (!canPortTrade(player, give_res, get_res)) return;
    uint8_t ratio = tradeRatio(player, give_res);
    players[player].resources[give_res] -= ratio;
    players[player].resources[get_res] += 1;
}

// ── Player data access ─────────────────────────────────────────────────────

const PlayerData& playerData(uint8_t player) {
    static const PlayerData empty = {};
    return (player < MAX_PLAYERS) ? players[player] : empty;
}

}  // namespace game
