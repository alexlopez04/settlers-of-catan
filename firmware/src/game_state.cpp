// =============================================================================
// game_state.cpp — Authoritative Catan game state.
//
// Pure logic (no Arduino / hardware dependencies); the I/O shell calls into
// these functions in response to player input and sensor events.
// =============================================================================

#include "game_state.h"
#include "board_topology.h"
#include "dice.h"
#include "core/rng.h"
#include <string.h>

namespace {

constexpr uint8_t NRES = (uint8_t)Res::COUNT;
constexpr uint8_t NDEV = (uint8_t)Dev::COUNT;

GamePhase    current_phase    = GamePhase::LOBBY;
uint8_t      num_players      = 0;
uint8_t      current_player_  = 0;

bool         player_connected_[MAX_PLAYERS] = {};
bool         player_ready_[MAX_PLAYERS]     = {};

// Resource counts per (player, resource).
uint8_t      res_count_[MAX_PLAYERS][NRES] = {};
// Bank supply per resource.
uint8_t      bank_supply_[NRES] = {};
// Last distribution per (player, resource).
uint8_t      last_distribution_[MAX_PLAYERS * NRES] = {};

// Dev cards.
uint8_t      dev_count_[MAX_PLAYERS][NDEV] = {};
uint8_t      dev_bought_this_turn_[MAX_PLAYERS][NDEV] = {};
uint8_t      dev_deck_[DEV_DECK_SIZE];
uint8_t      dev_deck_pos_      = 0;     // next index to draw
uint8_t      dev_deck_size_     = 0;     // 25 once shuffled, decremented on draw
bool         card_played_this_turn_ = false;

// Knights / VP bonuses.
uint8_t      knights_played_[MAX_PLAYERS] = {};
uint8_t      largest_army_player_ = NO_PLAYER;
uint8_t      longest_road_player_ = NO_PLAYER;
uint8_t      longest_road_length_ = 0;

// VP cache (public + total).
uint8_t      public_vp_[MAX_PLAYERS] = {};
uint8_t      total_vp_[MAX_PLAYERS]  = {};

// Pending purchases.
uint8_t      pending_road_buy_[MAX_PLAYERS]       = {};
uint8_t      pending_settlement_buy_[MAX_PLAYERS] = {};
uint8_t      pending_city_buy_[MAX_PLAYERS]       = {};
uint8_t      free_roads_remaining_[MAX_PLAYERS]   = {};

// Discard/steal.
uint8_t      discard_required_mask_  = 0;
uint8_t      discard_required_count_[MAX_PLAYERS] = {};
uint8_t      steal_eligible_mask_    = 0;

// Trade.
game::PendingTrade pending_trade_ = { NO_PLAYER, NO_PLAYER, {0,0,0,0,0}, {0,0,0,0,0} };

// Pieces.
VertexState  vertices[VERTEX_COUNT];
EdgeState    edges[EDGE_COUNT];

uint8_t      robber_tile_ = 0xFF;
uint8_t      last_reject_reason_ = 0;
Difficulty   difficulty_ = Difficulty::NORMAL;

// Number reveal order (Catan standard: 2,3,…,6,8,…,12)
constexpr uint8_t kRevealOrder[] = { 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };
constexpr uint8_t REVEAL_COUNT = sizeof(kRevealOrder);
uint8_t reveal_index = 0;

// Dice
uint8_t die1_ = 0, die2_ = 0;
bool    has_rolled_ = false;

// Initial placement (snake draft)
uint8_t setup_round_        = 0;
uint8_t setup_turn_         = 0;
uint8_t setup_first_player_ = 0;

// ── Helpers ───────────────────────────────────────────────────────────────
inline uint8_t costAt(const ResourceCost& c, uint8_t r) {
    switch (r) {
        case 0: return c.lumber;
        case 1: return c.wool;
        case 2: return c.grain;
        case 3: return c.brick;
        case 4: return c.ore;
    }
    return 0;
}

}  // anonymous namespace

namespace game {

// ── init ────────────────────────────────────────────────────────────────────
void init() {
    current_phase   = GamePhase::LOBBY;
    num_players     = 0;
    current_player_ = 0;
    reveal_index    = 0;
    robber_tile_    = 0xFF;
    last_reject_reason_ = 0;
    difficulty_     = Difficulty::NORMAL;
    die1_ = die2_   = 0;
    has_rolled_     = false;
    setup_round_        = 0;
    setup_turn_         = 0;
    setup_first_player_ = 0;

    memset(player_connected_, 0, sizeof(player_connected_));
    memset(player_ready_,     0, sizeof(player_ready_));
    memset(res_count_,        0, sizeof(res_count_));
    memset(last_distribution_, 0, sizeof(last_distribution_));
    memset(dev_count_,        0, sizeof(dev_count_));
    memset(dev_bought_this_turn_, 0, sizeof(dev_bought_this_turn_));
    memset(knights_played_,   0, sizeof(knights_played_));
    memset(public_vp_,        0, sizeof(public_vp_));
    memset(total_vp_,         0, sizeof(total_vp_));
    memset(pending_road_buy_, 0, sizeof(pending_road_buy_));
    memset(pending_settlement_buy_, 0, sizeof(pending_settlement_buy_));
    memset(pending_city_buy_, 0, sizeof(pending_city_buy_));
    memset(free_roads_remaining_, 0, sizeof(free_roads_remaining_));
    memset(discard_required_count_, 0, sizeof(discard_required_count_));
    discard_required_mask_  = 0;
    steal_eligible_mask_    = 0;
    largest_army_player_    = NO_PLAYER;
    longest_road_player_    = NO_PLAYER;
    longest_road_length_    = 0;
    card_played_this_turn_  = false;
    dev_deck_pos_           = 0;
    dev_deck_size_          = 0;
    pending_trade_ = { NO_PLAYER, NO_PLAYER, {0,0,0,0,0}, {0,0,0,0,0} };

    for (uint8_t r = 0; r < NRES; ++r) bank_supply_[r] = BANK_INITIAL_PER_RESOURCE;

    for (uint8_t i = 0; i < VERTEX_COUNT; ++i) {
        vertices[i].owner   = NO_PLAYER;
        vertices[i].is_city = false;
    }
    for (uint8_t i = 0; i < EDGE_COUNT; ++i) edges[i].owner = NO_PLAYER;
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
        case GamePhase::DISCARD:           return "DISCARD";
        case GamePhase::GAME_OVER:         return "GAME_OVER";
    }
    return "?";
}

// ── Players ─────────────────────────────────────────────────────────────────
uint8_t numPlayers() { return num_players; }
void setNumPlayers(uint8_t n) { if (n <= MAX_PLAYERS) num_players = n; }
bool playerConnected(uint8_t id) { return (id < MAX_PLAYERS) ? player_connected_[id] : false; }
void setPlayerConnected(uint8_t id, bool c) { if (id < MAX_PLAYERS) player_connected_[id] = c; }
uint8_t connectedMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (player_connected_[i]) m |= (1u << i);
    return m;
}

// ── Turn ────────────────────────────────────────────────────────────────────
uint8_t currentPlayer() { return current_player_; }
void setCurrentPlayer(uint8_t id) { if (id < MAX_PLAYERS) current_player_ = id; }
void nextTurn() {
    has_rolled_ = false;
    die1_ = die2_ = 0;
    card_played_this_turn_ = false;
    if (num_players == 0) return;
    // Move newly-bought dev cards out of "bought this turn" so the player
    // who just took their turn can play them next time.
    clearDevCardsBoughtThisTurn(current_player_);
    // Free Roads / pending purchases survive the turn — they belong to
    // whoever has them. Free Roads is per-player so we leave intact.
    current_player_ = (current_player_ + 1u) % num_players;
    // The arriving player can now play any cards they bought before this turn.
    clearDevCardsBoughtThisTurn(current_player_);
}

// ── Resources ──────────────────────────────────────────────────────────────
uint8_t resCount(uint8_t p, Res r) {
    return (p < MAX_PLAYERS && (uint8_t)r < NRES) ? res_count_[p][(uint8_t)r] : 0;
}
void setResCount(uint8_t p, Res r, uint8_t c) {
    if (p < MAX_PLAYERS && (uint8_t)r < NRES) res_count_[p][(uint8_t)r] = c;
}
uint8_t totalCards(uint8_t p) {
    if (p >= MAX_PLAYERS) return 0;
    uint16_t s = 0;
    for (uint8_t r = 0; r < NRES; ++r) s += res_count_[p][r];
    return (uint8_t)((s > 255) ? 255 : s);
}
void addRes(uint8_t p, Res r, uint8_t n) {
    if (p >= MAX_PLAYERS || (uint8_t)r >= NRES) return;
    uint16_t v = (uint16_t)res_count_[p][(uint8_t)r] + n;
    res_count_[p][(uint8_t)r] = (uint8_t)((v > 255) ? 255 : v);
}
bool spendRes(uint8_t p, Res r, uint8_t n) {
    if (p >= MAX_PLAYERS || (uint8_t)r >= NRES) return false;
    if (res_count_[p][(uint8_t)r] < n) return false;
    res_count_[p][(uint8_t)r] -= n;
    // Returned to bank.
    returnToBank(r, n);
    return true;
}
bool hasRes(uint8_t p, const ResourceCost& c) {
    if (p >= MAX_PLAYERS) return false;
    for (uint8_t r = 0; r < NRES; ++r) {
        if (res_count_[p][r] < costAt(c, r)) return false;
    }
    return true;
}
bool spendCost(uint8_t p, const ResourceCost& c) {
    if (!hasRes(p, c)) return false;
    for (uint8_t r = 0; r < NRES; ++r) {
        uint8_t n = costAt(c, r);
        if (n) {
            res_count_[p][r] -= n;
            returnToBank((Res)r, n);
        }
    }
    return true;
}
void refundCost(uint8_t p, const ResourceCost& c) {
    if (p >= MAX_PLAYERS) return;
    for (uint8_t r = 0; r < NRES; ++r) {
        uint8_t n = costAt(c, r);
        if (n) {
            // Pull back from bank if available.
            uint8_t take = (bank_supply_[r] >= n) ? n : bank_supply_[r];
            bank_supply_[r] = (uint8_t)(bank_supply_[r] - take);
            uint16_t v = (uint16_t)res_count_[p][r] + take;
            res_count_[p][r] = (uint8_t)((v > 255) ? 255 : v);
        }
    }
}

uint8_t bankSupply(Res r) {
    return ((uint8_t)r < NRES) ? bank_supply_[(uint8_t)r] : 0;
}
void setBankSupply(Res r, uint8_t n) {
    if ((uint8_t)r < NRES) bank_supply_[(uint8_t)r] = n;
}
bool drawFromBank(Res r, uint8_t n) {
    if ((uint8_t)r >= NRES) return false;
    if (bank_supply_[(uint8_t)r] < n) return false;
    bank_supply_[(uint8_t)r] -= n;
    return true;
}
void returnToBank(Res r, uint8_t n) {
    if ((uint8_t)r >= NRES) return;
    uint16_t v = (uint16_t)bank_supply_[(uint8_t)r] + n;
    bank_supply_[(uint8_t)r] = (uint8_t)((v > 255) ? 255 : v);
}
bool bankHasAll(const ResourceCost& c) {
    for (uint8_t r = 0; r < NRES; ++r) if (bank_supply_[r] < costAt(c, r)) return false;
    return true;
}

const uint8_t* lastDistribution() { return last_distribution_; }
void clearLastDistribution() { memset(last_distribution_, 0, sizeof(last_distribution_)); }
void markLastDistribution(uint8_t p, Res r, uint8_t n) {
    if (p >= MAX_PLAYERS || (uint8_t)r >= NRES) return;
    uint16_t v = (uint16_t)last_distribution_[p * NRES + (uint8_t)r] + n;
    last_distribution_[p * NRES + (uint8_t)r] = (uint8_t)((v > 255) ? 255 : v);
}

// Distribute resources for `roll`. Honours bank-depletion: if the bank
// can't cover all claimants of a resource, no one gets any of that type.
uint8_t distributeResources(uint8_t roll) {
    clearLastDistribution();
    if (roll == 7) return 0;

    // First pass: tally total demand per resource.
    uint8_t demand[NRES] = {};
    uint8_t per_player[MAX_PLAYERS][NRES] = {};

    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        if (g_tile_state[t].number != roll) continue;
        if (t == robber_tile_) continue;
        Biome b = g_tile_state[t].biome;
        uint8_t r_idx = biomeToResource(b);
        if (r_idx == NONE) continue;     // desert
        const TileDef& td = TILE_TOPO[t];
        for (uint8_t i = 0; i < 6; ++i) {
            uint8_t v = td.vertices[i];
            if (v >= VERTEX_COUNT) continue;
            uint8_t owner = vertices[v].owner;
            if (owner >= MAX_PLAYERS) continue;
            uint8_t qty = vertices[v].is_city ? 2 : 1;
            per_player[owner][r_idx] += qty;
            demand[r_idx] += qty;
        }
    }

    uint16_t total = 0;
    for (uint8_t r = 0; r < NRES; ++r) {
        if (demand[r] == 0) continue;
        if (bank_supply_[r] < demand[r]) {
            // Insufficient supply — no one gets any of this resource (Catan rule).
            // Special case: if exactly one player is claiming and the bank
            // has SOME supply, the official rule still says "no one". We
            // implement strict mode: nobody gets any.
            continue;
        }
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            uint8_t n = per_player[p][r];
            if (n == 0) continue;
            bank_supply_[r] = (uint8_t)(bank_supply_[r] - n);
            addRes(p, (Res)r, n);
            markLastDistribution(p, (Res)r, n);
            total += n;
        }
    }
    return (uint8_t)((total > 255) ? 255 : total);
}

// ── Lobby readiness ─────────────────────────────────────────────────────────
bool playerReady(uint8_t p) { return (p < MAX_PLAYERS) ? player_ready_[p] : false; }
void setPlayerReady(uint8_t p, bool r) { if (p < MAX_PLAYERS) player_ready_[p] = r; }
uint8_t readyMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (player_ready_[i]) m |= (1u << i);
    return m;
}
void clearReady() { memset(player_ready_, 0, sizeof(player_ready_)); }

// ── Difficulty ───────────────────────────────────────────────────────────────
Difficulty difficulty() { return difficulty_; }
void setDifficulty(Difficulty d) { difficulty_ = d; }

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
void setVertexState(uint8_t v, uint8_t owner, bool is_city) {
    if (v >= VERTEX_COUNT) return;
    vertices[v].owner   = owner;
    vertices[v].is_city = is_city;
}
void setEdgeState(uint8_t e, uint8_t owner) {
    if (e >= EDGE_COUNT) return;
    edges[e].owner = owner;
}

uint8_t roadCount(uint8_t p) {
    if (p >= MAX_PLAYERS) return 0;
    uint8_t n = 0;
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) if (edges[e].owner == p) ++n;
    return n;
}
uint8_t settlementCount(uint8_t p) {
    if (p >= MAX_PLAYERS) return 0;
    uint8_t n = 0;
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v)
        if (vertices[v].owner == p && !vertices[v].is_city) ++n;
    return n;
}
uint8_t cityCount(uint8_t p) {
    if (p >= MAX_PLAYERS) return 0;
    uint8_t n = 0;
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v)
        if (vertices[v].owner == p && vertices[v].is_city) ++n;
    return n;
}

// ── Last reject reason ──────────────────────────────────────────────────────
uint8_t lastRejectReason()             { return last_reject_reason_; }
void    setLastRejectReason(uint8_t r) { last_reject_reason_ = r; }
void    clearLastRejectReason()        { last_reject_reason_ = 0; }

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
uint8_t revealIndex()          { return reveal_index; }
void    setRevealIndex(uint8_t i) { reveal_index = (i < REVEAL_COUNT) ? i : 0; }

// ── Initial placement — snake draft ────────────────────────────────────────
static uint8_t playerForTurn(uint8_t i, uint8_t n, uint8_t first) {
    if (n == 0) return 0;
    if (i < n) return (first + i) % n;
    return (uint8_t)((first + (uint8_t)(2 * n - 1u - i)) % n);
}

uint8_t setupRound()       { return setup_round_; }
uint8_t setupTurn()        { return setup_turn_; }
uint8_t setupFirstPlayer() { return setup_first_player_; }
void setSetupRound(uint8_t r)       { setup_round_ = r; }
void setSetupTurn(uint8_t t)        { setup_turn_  = t; }
void setSetupFirstPlayer(uint8_t fp) { setup_first_player_ = fp; }

void resetSetupRound(uint8_t first_player) {
    if (num_players == 0) return;
    setup_first_player_ = first_player % num_players;
    setup_turn_         = 0;
    setup_round_        = 1;
    current_player_     = playerForTurn(0, num_players, setup_first_player_);
}

bool advanceSetupTurn() {
    uint8_t total = (uint8_t)(2u * num_players);
    if (setup_turn_ + 1u >= total) return false;
    ++setup_turn_;
    setup_round_    = (setup_turn_ < num_players) ? 1 : 2;
    current_player_ = playerForTurn(setup_turn_, num_players, setup_first_player_);
    return true;
}

// ── Dice ────────────────────────────────────────────────────────────────────
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

// ── Dev cards ───────────────────────────────────────────────────────────────
void initDevDeck() {
    uint8_t i = 0;
    for (uint8_t k = 0; k < DEV_COUNT_KNIGHT;          ++k) dev_deck_[i++] = (uint8_t)Dev::KNIGHT;
    for (uint8_t k = 0; k < DEV_COUNT_VP;              ++k) dev_deck_[i++] = (uint8_t)Dev::VP;
    for (uint8_t k = 0; k < DEV_COUNT_ROAD_BUILDING;   ++k) dev_deck_[i++] = (uint8_t)Dev::ROAD_BUILDING;
    for (uint8_t k = 0; k < DEV_COUNT_YEAR_OF_PLENTY;  ++k) dev_deck_[i++] = (uint8_t)Dev::YEAR_OF_PLENTY;
    for (uint8_t k = 0; k < DEV_COUNT_MONOPOLY;        ++k) dev_deck_[i++] = (uint8_t)Dev::MONOPOLY;
    dev_deck_size_ = i;
    // Fisher-Yates shuffle.
    for (uint8_t j = (uint8_t)(dev_deck_size_ - 1); j > 0; --j) {
        uint8_t k = (uint8_t)core::rng::uniform((uint8_t)(j + 1));
        uint8_t tmp = dev_deck_[j];
        dev_deck_[j] = dev_deck_[k];
        dev_deck_[k] = tmp;
    }
    dev_deck_pos_ = 0;
}
uint8_t devDeckRemaining() {
    return (uint8_t)(dev_deck_size_ - dev_deck_pos_);
}
const uint8_t* devDeckData()    { return dev_deck_; }
uint8_t        devDeckPos()     { return dev_deck_pos_; }
uint8_t        devDeckSizeTotal() { return dev_deck_size_; }
void restoreDevDeck(const uint8_t* deck, uint8_t pos, uint8_t size) {
    if (size > DEV_DECK_SIZE) size = DEV_DECK_SIZE;
    memcpy(dev_deck_, deck, size);
    dev_deck_size_ = size;
    dev_deck_pos_  = (pos <= size) ? pos : size;
}
Dev drawDevCard(uint8_t player) {
    if (player >= MAX_PLAYERS) return Dev::COUNT;
    if (dev_deck_pos_ >= dev_deck_size_) return Dev::COUNT;
    Dev d = (Dev)dev_deck_[dev_deck_pos_++];
    dev_count_[player][(uint8_t)d]++;
    dev_bought_this_turn_[player][(uint8_t)d]++;
    return d;
}
uint8_t devCardCount(uint8_t p, Dev d) {
    return (p < MAX_PLAYERS && (uint8_t)d < NDEV) ? dev_count_[p][(uint8_t)d] : 0;
}
void setDevCardCount(uint8_t p, Dev d, uint8_t n) {
    if (p < MAX_PLAYERS && (uint8_t)d < NDEV) dev_count_[p][(uint8_t)d] = n;
}
uint8_t devCardBoughtThisTurn(uint8_t p, Dev d) {
    return (p < MAX_PLAYERS && (uint8_t)d < NDEV) ? dev_bought_this_turn_[p][(uint8_t)d] : 0;
}
void clearDevCardsBoughtThisTurn(uint8_t p) {
    if (p < MAX_PLAYERS) memset(dev_bought_this_turn_[p], 0, NDEV);
}
bool canPlayDevCard(uint8_t p, Dev d) {
    if (p >= MAX_PLAYERS || (uint8_t)d >= NDEV) return false;
    if (d == Dev::VP) return false;   // VP cards are passive
    if (card_played_this_turn_) return false;
    uint8_t total   = dev_count_[p][(uint8_t)d];
    uint8_t bought  = dev_bought_this_turn_[p][(uint8_t)d];
    return total > bought;
}

bool cardPlayedThisTurn() { return card_played_this_turn_; }
void setCardPlayedThisTurn(bool v) { card_played_this_turn_ = v; }

// ── Knights / Largest Army / Longest Road ──────────────────────────────────
uint8_t knightsPlayed(uint8_t p) {
    return (p < MAX_PLAYERS) ? knights_played_[p] : 0;
}
void incKnightsPlayed(uint8_t p) {
    if (p < MAX_PLAYERS) knights_played_[p]++;
    recomputeLargestArmy();
}
void setKnightsPlayed(uint8_t p, uint8_t n) {
    if (p < MAX_PLAYERS) knights_played_[p] = n;
}
uint8_t largestArmyPlayer() { return largest_army_player_; }
uint8_t longestRoadPlayer() { return longest_road_player_; }
uint8_t longestRoadLength() { return longest_road_length_; }
void setLargestArmyPlayer(uint8_t p)   { largest_army_player_ = p; }
void setLongestRoadPlayer(uint8_t p)   { longest_road_player_ = p; }
void setLongestRoadLength(uint8_t len) { longest_road_length_ = len; }

void recomputeLargestArmy() {
    // Holder keeps the title in a tie; only transfer if a candidate strictly
    // exceeds the current holder's count and meets the minimum.
    uint8_t best_count = 0;
    if (largest_army_player_ != NO_PLAYER)
        best_count = knights_played_[largest_army_player_];
    for (uint8_t p = 0; p < num_players; ++p) {
        if (p == largest_army_player_) continue;
        uint8_t k = knights_played_[p];
        if (k >= LARGEST_ARMY_MIN && k > best_count) {
            largest_army_player_ = p;
            best_count = k;
        }
    }
    if (largest_army_player_ != NO_PLAYER &&
        knights_played_[largest_army_player_] < LARGEST_ARMY_MIN) {
        largest_army_player_ = NO_PLAYER;
    }
}

// Longest-road DFS. The graph is the player's roads; an opponent's
// settlement/city on a junction breaks the path through that vertex.
namespace {

uint8_t longestPathFromEdge(uint8_t player, uint8_t start_edge,
                            uint8_t start_vertex,
                            uint8_t edges_visited[(EDGE_COUNT + 7) / 8]) {
    // DFS from the (vertex_into) end of `start_edge` outward.
    // Returns the number of ADDITIONAL edges reachable from start_vertex
    // (not counting start_edge, which is already tallied by the caller).
    uint8_t best = 0;
    const VertexDef& vd = VERTEX_TOPO[start_vertex];
    // If an opponent occupies start_vertex, the path cannot continue here.
    uint8_t v_owner = vertices[start_vertex].owner;
    if (v_owner != NO_PLAYER && v_owner != player) return 0;
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        if (e == start_edge) continue;
        if (edges[e].owner != player) continue;
        uint8_t byte_idx = (uint8_t)(e >> 3);
        uint8_t bit = (uint8_t)(1u << (e & 7));
        if (edges_visited[byte_idx] & bit) continue;
        edges_visited[byte_idx] |= bit;
        const EdgeDef& ed = EDGE_TOPO[e];
        uint8_t other = (ed.vertices[0] == start_vertex) ? ed.vertices[1] : ed.vertices[0];
        uint8_t len = (uint8_t)(1u + longestPathFromEdge(player, e, other, edges_visited));
        if (len > best) best = len;
        edges_visited[byte_idx] &= (uint8_t)~bit;
    }
    return best;
}

uint8_t longestRoadFor(uint8_t player) {
    uint8_t best = 0;
    uint8_t edges_visited[(EDGE_COUNT + 7) / 8];
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        if (edges[e].owner != player) continue;
        // Try starting from each endpoint.
        const EdgeDef& ed = EDGE_TOPO[e];
        for (uint8_t side = 0; side < 2; ++side) {
            uint8_t start_v = ed.vertices[side];
            uint8_t other_v = ed.vertices[side ^ 1];
            // Don't start a path "through" an opponent-blocked vertex.
            uint8_t v_owner = vertices[start_v].owner;
            if (v_owner != NO_PLAYER && v_owner != player) continue;
            (void)v_owner;
            memset(edges_visited, 0, sizeof(edges_visited));
            edges_visited[e >> 3] |= (uint8_t)(1u << (e & 7));
            uint8_t len = (uint8_t)(1u + longestPathFromEdge(player, e, other_v, edges_visited));
            if (len > best) best = len;
        }
    }
    return best;
}

}  // anonymous

void recomputeLongestRoad() {
    // Cache each player's current road length.
    uint8_t lengths[MAX_PLAYERS] = {};
    uint8_t max_len = 0;
    for (uint8_t p = 0; p < num_players; ++p) {
        lengths[p] = longestRoadFor(p);
        if (lengths[p] > max_len) max_len = lengths[p];
    }

    if (max_len < LONGEST_ROAD_MIN) {
        longest_road_player_ = NO_PLAYER;
        longest_road_length_ = 0;
        return;
    }

    // If the current holder still shares the maximum, they keep the card
    // (Catan tie rule: holder is never displaced by a tie).
    if (longest_road_player_ != NO_PLAYER &&
        lengths[longest_road_player_] == max_len) {
        longest_road_length_ = max_len;
        return;
    }

    // Holder does not have max_len — count who does.
    uint8_t count = 0;
    uint8_t new_holder = NO_PLAYER;
    for (uint8_t p = 0; p < num_players; ++p) {
        if (lengths[p] == max_len) {
            ++count;
            new_holder = p;
        }
    }

    if (count == 1) {
        // Unique new leader takes the card.
        longest_road_player_ = new_holder;
        longest_road_length_ = max_len;
    } else {
        // Multiple players tied and holder is not among them:
        // nobody holds the card until one player pulls ahead.
        longest_road_player_ = NO_PLAYER;
        longest_road_length_ = 0;
    }
}

// ── VP ──────────────────────────────────────────────────────────────────────
uint8_t publicVp(uint8_t p) { return (p < MAX_PLAYERS) ? public_vp_[p] : 0; }
uint8_t totalVp(uint8_t p)  { return (p < MAX_PLAYERS) ? total_vp_[p]  : 0; }

// Recomputes VP cache without touching bonus-holder fields.
// Used after a save/restore so the saved largest_army_player_ /
// longest_road_player_ values are preserved.
void recomputeVpCacheOnly() {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        uint8_t pub = 0;
        pub += settlementCount(p);
        pub += (uint8_t)(cityCount(p) * 2u);
        if (p == largest_army_player_) pub += 2;
        if (p == longest_road_player_) pub += 2;
        public_vp_[p] = pub;
        total_vp_[p]  = (uint8_t)(pub + dev_count_[p][(uint8_t)Dev::VP]);
    }
}
void recomputeVp() {
    recomputeLargestArmy();
    recomputeLongestRoad();
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        uint8_t pub = 0;
        pub += settlementCount(p) * 1u;
        pub += cityCount(p) * 2u;
        if (p == largest_army_player_) pub += 2;
        if (p == longest_road_player_) pub += 2;
        public_vp_[p] = pub;
        total_vp_[p]  = (uint8_t)(pub + dev_count_[p][(uint8_t)Dev::VP]);
    }
}

uint8_t checkWinner() {
    // Use total VP (including hidden VP cards) — any player at 10+ wins.
    // Note: per Catan rules a player only wins on their own turn (you can't
    // be forced to win on someone else's turn). We require it to be the
    // player's turn for the win to register.
    if (total_vp_[current_player_] >= VP_TO_WIN) return current_player_;
    return NO_PLAYER;
}

// ── Pending purchases ──────────────────────────────────────────────────────
uint8_t pendingRoadBuy(uint8_t p)        { return (p < MAX_PLAYERS) ? pending_road_buy_[p] : 0; }
uint8_t pendingSettlementBuy(uint8_t p)  { return (p < MAX_PLAYERS) ? pending_settlement_buy_[p] : 0; }
uint8_t pendingCityBuy(uint8_t p)        { return (p < MAX_PLAYERS) ? pending_city_buy_[p] : 0; }
void    setPendingRoadBuy(uint8_t p, uint8_t n)       { if (p < MAX_PLAYERS) pending_road_buy_[p] = n; }
void    setPendingSettlementBuy(uint8_t p, uint8_t n) { if (p < MAX_PLAYERS) pending_settlement_buy_[p] = n; }
void    setPendingCityBuy(uint8_t p, uint8_t n)       { if (p < MAX_PLAYERS) pending_city_buy_[p] = n; }
void    addPendingRoadBuy(uint8_t p)       { if (p < MAX_PLAYERS) pending_road_buy_[p]++; }
void    addPendingSettlementBuy(uint8_t p) { if (p < MAX_PLAYERS) pending_settlement_buy_[p]++; }
void    addPendingCityBuy(uint8_t p)       { if (p < MAX_PLAYERS) pending_city_buy_[p]++; }
bool    consumePendingRoadBuy(uint8_t p)       { if (p < MAX_PLAYERS && pending_road_buy_[p]) { pending_road_buy_[p]--; return true; } return false; }
bool    consumePendingSettlementBuy(uint8_t p) { if (p < MAX_PLAYERS && pending_settlement_buy_[p]) { pending_settlement_buy_[p]--; return true; } return false; }
bool    consumePendingCityBuy(uint8_t p)       { if (p < MAX_PLAYERS && pending_city_buy_[p]) { pending_city_buy_[p]--; return true; } return false; }

uint8_t freeRoadsRemaining(uint8_t p) { return (p < MAX_PLAYERS) ? free_roads_remaining_[p] : 0; }
void    setFreeRoadsRemaining(uint8_t p, uint8_t n) { if (p < MAX_PLAYERS) free_roads_remaining_[p] = n; }

// ── Discard / Steal ────────────────────────────────────────────────────────
uint8_t discardRequiredMask() { return discard_required_mask_; }
uint8_t discardRequiredCount(uint8_t p) {
    return (p < MAX_PLAYERS) ? discard_required_count_[p] : 0;
}
void setDiscardRequired(uint8_t p, uint8_t count) {
    if (p >= MAX_PLAYERS) return;
    discard_required_count_[p] = count;
    if (count) discard_required_mask_ |= (1u << p);
    else       discard_required_mask_ &= (uint8_t)~(1u << p);
}
void clearDiscardRequired(uint8_t p) { setDiscardRequired(p, 0); }
void clearAllDiscardRequired() {
    discard_required_mask_ = 0;
    memset(discard_required_count_, 0, sizeof(discard_required_count_));
}
uint8_t stealEligibleMask() { return steal_eligible_mask_; }
void    setStealEligibleMask(uint8_t m) { steal_eligible_mask_ = m; }
uint8_t recomputeStealEligibleMask() {
    uint8_t mask = 0;
    if (robber_tile_ >= TILE_COUNT) { steal_eligible_mask_ = 0; return 0; }
    const TileDef& td = TILE_TOPO[robber_tile_];
    for (uint8_t i = 0; i < 6; ++i) {
        uint8_t v = td.vertices[i];
        if (v >= VERTEX_COUNT) continue;
        uint8_t owner = vertices[v].owner;
        if (owner >= MAX_PLAYERS) continue;
        if (owner == current_player_) continue;
        if (totalCards(owner) == 0) continue;
        mask |= (1u << owner);
    }
    steal_eligible_mask_ = mask;
    return mask;
}

// ── Trade ──────────────────────────────────────────────────────────────────
const PendingTrade& pendingTrade() { return pending_trade_; }
bool hasPendingTrade() { return pending_trade_.from != NO_PLAYER; }
void setPendingTrade(uint8_t from, uint8_t to,
                     const uint8_t offer[5], const uint8_t want[5]) {
    pending_trade_.from = from;
    pending_trade_.to   = to;
    for (uint8_t i = 0; i < 5; ++i) {
        pending_trade_.offer[i] = offer[i];
        pending_trade_.want[i]  = want[i];
    }
}
void clearPendingTrade() {
    pending_trade_.from = NO_PLAYER;
    pending_trade_.to   = NO_PLAYER;
    memset(pending_trade_.offer, 0, sizeof(pending_trade_.offer));
    memset(pending_trade_.want,  0, sizeof(pending_trade_.want));
}

}  // namespace game
