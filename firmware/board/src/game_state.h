#pragma once
// =============================================================================
// game_state.h — Catan game state owned by the central board.
//
// The board is the authoritative source of truth for ALL game state:
//   - phase / turn / setup snake order
//   - tile layout (biomes + numbers) and the robber tile
//   - piece placement (settlement / city / road ownership)
//   - dice rolls and the number-reveal order
//   - resource counts per player + bank supply
//   - development card deck + per-player inventory
//   - knights played, longest road, largest army
//   - victory points (computed from placements + cards + bonuses)
//   - pending purchases (road/settlement/city) and free roads
//   - pending trade (one offer at a time)
//   - robber-discard mask + robber-steal eligibility mask
//
// Mobile clients no longer "report" anything except their actions; the
// board computes everything else.
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
    DISCARD,
    GAME_OVER
};

static constexpr uint8_t NO_PLAYER = 0xFF;

// Resource indices — match catan.proto ResourceType.
enum class Res : uint8_t {
    LUMBER = 0,
    WOOL   = 1,
    GRAIN  = 2,
    BRICK  = 3,
    ORE    = 4,
    COUNT
};

// Development card type indices — match catan.proto DevCardType.
enum class Dev : uint8_t {
    KNIGHT          = 0,
    VP              = 1,
    ROAD_BUILDING   = 2,
    YEAR_OF_PLENTY  = 3,
    MONOPOLY        = 4,
    COUNT
};

// Standard Catan deck composition (totals 25 cards).
constexpr uint8_t DEV_COUNT_KNIGHT          = 14;
constexpr uint8_t DEV_COUNT_VP              =  5;
constexpr uint8_t DEV_COUNT_ROAD_BUILDING   =  2;
constexpr uint8_t DEV_COUNT_YEAR_OF_PLENTY  =  2;
constexpr uint8_t DEV_COUNT_MONOPOLY        =  2;
constexpr uint8_t DEV_DECK_SIZE             = 25;

// Bank supply per resource (Catan standard: 19 of each).
constexpr uint8_t BANK_INITIAL_PER_RESOURCE = 19;

// Building costs (per Catan rules).
struct ResourceCost { uint8_t lumber, wool, grain, brick, ore; };
constexpr ResourceCost ROAD_COST       { 1, 0, 0, 1, 0 };
constexpr ResourceCost SETTLEMENT_COST { 1, 1, 1, 1, 0 };
constexpr ResourceCost CITY_COST       { 0, 0, 2, 0, 3 };
constexpr ResourceCost DEV_CARD_COST   { 0, 1, 1, 0, 1 };

// Per-player piece limits (Catan standard).
constexpr uint8_t MAX_ROADS_PER_PLAYER       = 15;
constexpr uint8_t MAX_SETTLEMENTS_PER_PLAYER =  5;
constexpr uint8_t MAX_CITIES_PER_PLAYER      =  4;

// Discard threshold: any player with > this many cards must discard half on a 7.
constexpr uint8_t DISCARD_THRESHOLD = 7;

// Largest Army threshold (knights played).
constexpr uint8_t LARGEST_ARMY_MIN = 3;

// Longest Road threshold.
constexpr uint8_t LONGEST_ROAD_MIN = 5;

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

// Public VP per player (used for broadcast / win check excluding hidden VP cards).
uint8_t publicVp(uint8_t player);
// True total VP for the player (including hidden VP dev cards). Used for the
// winner check; only revealed publicly when the game ends.
uint8_t totalVp(uint8_t player);
// Recompute all VP-related caches (settlements, cities, longest road,
// largest army, hidden cards). Call after any mutation that could affect VP.
void    recomputeVp();
uint8_t checkWinner();   // returns player id or NO_PLAYER (uses totalVp)

// Resources (board-authoritative).
uint8_t resCount(uint8_t player, Res r);
void    setResCount(uint8_t player, Res r, uint8_t count);
uint8_t totalCards(uint8_t player);
// Add/subtract — clamped at 0 / 255 / bank supply where appropriate.
void    addRes(uint8_t player, Res r, uint8_t n);
bool    spendRes(uint8_t player, Res r, uint8_t n);   // returns false if insufficient
bool    hasRes(uint8_t player, const ResourceCost& c);
bool    spendCost(uint8_t player, const ResourceCost& c);   // returns false if insufficient
void    refundCost(uint8_t player, const ResourceCost& c);

// Bank supply
uint8_t bankSupply(Res r);
void    setBankSupply(Res r, uint8_t n);
bool    drawFromBank(Res r, uint8_t n);   // false if insufficient
void    returnToBank(Res r, uint8_t n);
bool    bankHasAll(const ResourceCost& c);

// Resources distributed during the most recent successful roll. Index =
// player*5 + resource. Cleared at the start of the next roll/turn end.
const uint8_t* lastDistribution();   // length 4*5 = 20
void           clearLastDistribution();
void           markLastDistribution(uint8_t player, Res r, uint8_t n);

// Distribute resources from a given dice roll. Iterates all 19 tiles,
// pays each settlement (1) / city (2) adjacent to a producing tile, and
// honours the bank-depletion rule (if the bank can't cover ALL claimants
// of a resource type, no one gets any of that type for the roll). Records
// distribution into `lastDistribution()`. Returns the total cards dealt.
uint8_t distributeResources(uint8_t roll);

// Lobby readiness — toggled by ACTION_READY in PHASE_LOBBY.
bool    playerReady(uint8_t player);
void    setPlayerReady(uint8_t player, bool ready);
uint8_t readyMask();
void    clearReady();

// Board difficulty (set by Player 0 in the lobby, persisted until a new game starts).
Difficulty difficulty();
void       setDifficulty(Difficulty d);

// Ownership
const VertexState& vertexState(uint8_t vertex_id);
const EdgeState&   edgeState(uint8_t edge_id);
void placeSettlement(uint8_t vertex_id, uint8_t player);
void upgradeToCity(uint8_t vertex_id);
void placeRoad(uint8_t edge_id, uint8_t player);
// Direct setters for save/restore.
void setVertexState(uint8_t vertex_id, uint8_t owner, bool is_city);
void setEdgeState(uint8_t edge_id, uint8_t owner);

// Per-player piece counts (counted from ownership tables).
uint8_t roadCount(uint8_t player);
uint8_t settlementCount(uint8_t player);   // settlements only (not cities)
uint8_t cityCount(uint8_t player);

// Last placement / action rejection — mirrors core::RejectReason.
uint8_t lastRejectReason();
void    setLastRejectReason(uint8_t reason);
void    clearLastRejectReason();

// Robber
uint8_t robberTile();
void    setRobberTile(uint8_t tile_id);

// Number reveal
uint8_t currentRevealNumber();
bool    advanceReveal();
void    resetReveal();
uint8_t revealIndex();          // raw index into kRevealOrder[] (for save/restore)
void    setRevealIndex(uint8_t i);

// Initial placement — snake draft
uint8_t setupRound();        // 1 = forward, 2 = reverse
uint8_t setupTurn();         // 0-based position in the 2*N sequence
uint8_t setupFirstPlayer();
void    resetSetupRound(uint8_t first_player);
bool    advanceSetupTurn();
// Direct setters for save/restore (bypass derived recalculation).
void    setSetupRound(uint8_t r);
void    setSetupTurn(uint8_t t);
void    setSetupFirstPlayer(uint8_t fp);

// Dice
uint8_t lastDie1();
uint8_t lastDie2();
uint8_t lastDiceTotal();
void    rollDice();
bool    hasRolled();
void    setHasRolled(bool rolled);
void    clearDice();

// ── Development cards ──────────────────────────────────────────────────────
// Initialise the deck (called automatically on START_GAME). Shuffles the
// 25 cards using core::rng.
void    initDevDeck();
uint8_t devDeckRemaining();
// Draw the next card from the deck for `player`. Marks the card as
// "bought this turn" so it cannot be played until the player's next turn.
// Returns Dev::COUNT if the deck is empty.
Dev     drawDevCard(uint8_t player);
uint8_t devCardCount(uint8_t player, Dev d);
void    setDevCardCount(uint8_t player, Dev d, uint8_t count);
// Dev-deck internals needed for save/restore.
const uint8_t* devDeckData();           // pointer to the 25-card shuffled array
uint8_t        devDeckPos();            // next-draw index
uint8_t        devDeckSizeTotal();      // total deck size (25 once initialised)
void    restoreDevDeck(const uint8_t* deck, uint8_t pos, uint8_t size);
// Cards bought this turn — these are NOT playable until the next turn.
uint8_t devCardBoughtThisTurn(uint8_t player, Dev d);
void    clearDevCardsBoughtThisTurn(uint8_t player);
bool    canPlayDevCard(uint8_t player, Dev d);   // owns one not bought-this-turn AND not already played one

bool    cardPlayedThisTurn();
void    setCardPlayedThisTurn(bool v);

// Knights / Largest Army / Longest Road
uint8_t knightsPlayed(uint8_t player);
void    incKnightsPlayed(uint8_t player);
uint8_t largestArmyPlayer();   // NO_PLAYER if none
uint8_t longestRoadPlayer();   // NO_PLAYER if none
uint8_t longestRoadLength();
void    recomputeLargestArmy();
void    recomputeLongestRoad();
// Direct setters for save/restore (preserve tie-breaking history).
void    setKnightsPlayed(uint8_t player, uint8_t n);
void    setLargestArmyPlayer(uint8_t p);
void    setLongestRoadPlayer(uint8_t p);
void    setLongestRoadLength(uint8_t len);
// Recompute public_vp_ / total_vp_ without touching the bonus-holder fields.
void    recomputeVpCacheOnly();

// Pending purchases (per piece type, per player).
uint8_t pendingRoadBuy(uint8_t player);
uint8_t pendingSettlementBuy(uint8_t player);
uint8_t pendingCityBuy(uint8_t player);
void    setPendingRoadBuy(uint8_t player, uint8_t n);       // direct setter for restore
void    setPendingSettlementBuy(uint8_t player, uint8_t n); // direct setter for restore
void    setPendingCityBuy(uint8_t player, uint8_t n);       // direct setter for restore
void    addPendingRoadBuy(uint8_t player);
void    addPendingSettlementBuy(uint8_t player);
void    addPendingCityBuy(uint8_t player);
bool    consumePendingRoadBuy(uint8_t player);
bool    consumePendingSettlementBuy(uint8_t player);
bool    consumePendingCityBuy(uint8_t player);

uint8_t freeRoadsRemaining(uint8_t player);
void    setFreeRoadsRemaining(uint8_t player, uint8_t n);

// ── Robber discard / steal ─────────────────────────────────────────────────
uint8_t discardRequiredMask();
uint8_t discardRequiredCount(uint8_t player);
void    setDiscardRequired(uint8_t player, uint8_t count);
void    clearDiscardRequired(uint8_t player);
void    clearAllDiscardRequired();
// After PLACE_ROBBER, compute who is eligible to be stolen from. Sets
// the mask on `state`.
uint8_t recomputeStealEligibleMask();
uint8_t stealEligibleMask();
void    setStealEligibleMask(uint8_t mask);

// ── Trade ──────────────────────────────────────────────────────────────────
struct PendingTrade {
    uint8_t from;          // NO_PLAYER if no pending trade
    uint8_t to;            // NO_PLAYER for open offer
    uint8_t offer[5];      // counts of L,W,G,B,O the offerer gives
    uint8_t want[5];       // counts of L,W,G,B,O the offerer wants
};
const PendingTrade& pendingTrade();
void    setPendingTrade(uint8_t from, uint8_t to,
                        const uint8_t offer[5], const uint8_t want[5]);
void    clearPendingTrade();
bool    hasPendingTrade();

}  // namespace game
