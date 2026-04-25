// =============================================================================
// native/sim_main.cpp — Host-side simulation harness for the board FSM.
//
// Compiled by `pio run -e native -t exec` (see firmware/board/platformio.ini).
// The harness constructs a `core::StateMachine`, plays scripted scenarios,
// and checks observable state. It exits 0 if every assertion held.
//
// The simulation exercises only the pure modules (core/, game_state,
// board_topology, dice) — no Arduino, no LEDs, no comms.
// =============================================================================

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#include "core/state_machine.h"
#include "core/events.h"
#include "core/rule_engine.h"
#include "core/rng.h"
#include "game_state.h"
#include "board_topology.h"
#include "config.h"

using core::ActionKind;
using core::EffectKind;
using core::RejectReason;
using core::StateMachine;

// ── Minimal test harness ─────────────────────────────────────────────────
static int g_checks = 0;
static int g_failed = 0;
static const char* g_current_test = "";

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failed;                                                       \
            std::fprintf(stderr, "  FAIL [%s] %s:%d: %s\n",                   \
                         g_current_test, __FILE__, __LINE__, #cond);          \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        ++g_checks;                                                           \
        auto __a = (long long)(a);                                            \
        auto __b = (long long)(b);                                            \
        if (__a != __b) {                                                     \
            ++g_failed;                                                       \
            std::fprintf(stderr,                                              \
                         "  FAIL [%s] %s:%d: %s (%lld) != %s (%lld)\n",       \
                         g_current_test, __FILE__, __LINE__,                  \
                         #a, __a, #b, __b);                                   \
        }                                                                     \
    } while (0)

// ── Helpers ───────────────────────────────────────────────────────────────

// Drain effects into a vector so tests can inspect what was emitted.
static std::vector<core::Effect> drainEffects(StateMachine& sm) {
    std::vector<core::Effect> out;
    core::Effect e;
    while (sm.pollEffect(e)) out.push_back(e);
    return out;
}

static bool containsEffect(const std::vector<core::Effect>& es,
                           EffectKind k) {
    for (const auto& e : es) if (e.kind == k) return true;
    return false;
}

static int countEffect(const std::vector<core::Effect>& es, EffectKind k) {
    int n = 0;
    for (const auto& e : es) if (e.kind == k) ++n;
    return n;
}

static const core::Effect* findEffect(const std::vector<core::Effect>& es,
                                      EffectKind k) {
    for (const auto& e : es) if (e.kind == k) return &e;
    return nullptr;
}

// Fresh game + FSM, deterministic RNG seed.
static void resetScenario(StateMachine& sm, uint32_t seed = 1) {
    game::init();
    sm.reset();
    core::rng::seed(seed);
    game::setPhase(GamePhase::LOBBY);
}

// Simulate the board's I2C poll: player i's phone is connected.
// In the real firmware the main loop mirrors comm::connectedMask() into
// game state every tick; here we just poke it directly.
static void markConnected(uint8_t n) {
    for (uint8_t p = 0; p < n; ++p) {
        game::setPlayerConnected(p, true);
    }
    game::setNumPlayers(n);
}

// Walk the LOBBY → PLAYING sequence, returning the FSM primed to play.
// - Connects `num` players (ids 0..num-1).
// - Uses a deterministic seed so first_player_ is reproducible.
static void bootToPlaying(StateMachine& sm, uint8_t num, uint32_t seed = 1) {
    resetScenario(sm, seed);

    markConnected(num);
    sm.tick(0);
    drainEffects(sm);

    sm.handlePlayerAction(0, ActionKind::START_GAME);
    sm.tick(1);
    drainEffects(sm);  // BOARD_SETUP entered, board randomized.

    sm.handlePlayerAction(0, ActionKind::NEXT_NUMBER);
    sm.tick(2);
    drainEffects(sm);  // NUMBER_REVEAL entered.

    // Step through all 10 numbers (2..12 without 7).
    for (int i = 0; i < 10; ++i) {
        sm.handlePlayerAction(0, ActionKind::NEXT_NUMBER);
        sm.tick((uint32_t)(3 + i));
        drainEffects(sm);
    }
    // Should now be in INITIAL_PLACEMENT.
}

// Find an edge incident to vertex v where neither endpoint is vertex v's
// neighbour already owned by someone else. Used to construct valid initial
// road placements near a just-placed settlement.
static uint8_t anyEdgeAtVertex(uint8_t v) {
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e < EDGE_COUNT) return e;
    }
    return 0xFF;
}

// Find a vertex adjacent (distance-1) to `v` via any edge.
static uint8_t anyNeighbourVertex(uint8_t v) {
    const VertexDef& vd = VERTEX_TOPO[v];
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t e = vd.edges[i];
        if (e >= EDGE_COUNT) continue;
        const EdgeDef& ed = EDGE_TOPO[e];
        uint8_t other = (ed.vertices[0] == v) ? ed.vertices[1] : ed.vertices[0];
        if (other < VERTEX_COUNT) return other;
    }
    return 0xFF;
}

// =============================================================================
// Scenarios
// =============================================================================

static void test_lobby_to_setup() {
    g_current_test = "lobby_to_setup";
    StateMachine sm;
    resetScenario(sm);

    // Connect two players (simulated I2C mask update).
    markConnected(2);
    sm.tick(0);
    CHECK_EQ((int)game::numPlayers(), 2);
    CHECK(game::playerConnected(0));
    CHECK(game::playerConnected(1));
    CHECK_EQ((int)game::phase(), (int)GamePhase::LOBBY);
    drainEffects(sm);

    // Now START_GAME should transition to BOARD_SETUP and emit PHASE_ENTERED.
    sm.handlePlayerAction(0, ActionKind::START_GAME);
    sm.tick(1);
    auto effs = drainEffects(sm);
    CHECK_EQ((int)game::phase(), (int)GamePhase::BOARD_SETUP);
    CHECK(containsEffect(effs, EffectKind::PHASE_ENTERED));

    // Next tick randomizes the board.
    sm.tick(2);
    auto effs2 = drainEffects(sm);
    CHECK(containsEffect(effs2, EffectKind::BOARD_RANDOMIZED));
}

static void test_full_number_reveal() {
    g_current_test = "full_number_reveal";
    StateMachine sm;
    resetScenario(sm);

    markConnected(3);
    sm.tick(0);
    sm.handlePlayerAction(0, ActionKind::START_GAME);
    sm.tick(1);
    drainEffects(sm);

    sm.handlePlayerAction(0, ActionKind::NEXT_NUMBER);
    sm.tick(2);
    CHECK_EQ((int)game::phase(), (int)GamePhase::NUMBER_REVEAL);
    drainEffects(sm);

    // Ten numbers: 2,3,4,5,6,8,9,10,11,12.
    int reveal_events = 0;
    for (int i = 0; i < 10; ++i) {
        sm.handlePlayerAction(0, ActionKind::NEXT_NUMBER);
        sm.tick((uint32_t)(3 + i));
        auto es = drainEffects(sm);
        reveal_events += countEffect(es, EffectKind::REVEAL_NUMBER_CHANGED);
    }
    CHECK(reveal_events >= 10);
    CHECK_EQ((int)game::phase(), (int)GamePhase::INITIAL_PLACEMENT);
}

static void test_distance_rule() {
    g_current_test = "distance_rule";
    StateMachine sm;
    bootToPlaying(sm, 3);
    CHECK_EQ((int)game::phase(), (int)GamePhase::INITIAL_PLACEMENT);

    uint8_t cp = game::currentPlayer();
    const uint8_t v = 0;
    uint8_t v_adj = anyNeighbourVertex(v);
    CHECK(v_adj != 0xFF);

    // First, place a settlement at v.
    sm.onVertexPresent(v);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::PLACED_SETTLEMENT));
    CHECK_EQ((int)game::vertexState(v).owner, (int)cp);

    // Attempt to place an adjacent settlement → must be rejected.
    sm.onVertexPresent(v_adj);
    es = drainEffects(sm);
    const core::Effect* rej = findEffect(es, EffectKind::PLACEMENT_REJECTED);
    CHECK(rej != nullptr);
    if (rej) CHECK_EQ((int)rej->b, (int)RejectReason::TOO_CLOSE_TO_SETTLEMENT);
    CHECK_EQ((int)game::vertexState(v_adj).owner, (int)NO_PLAYER);
}

static void test_initial_road_must_touch_settlement() {
    g_current_test = "initial_road_requires_settlement";
    StateMachine sm;
    bootToPlaying(sm, 3);

    // Place a road with no settlement anywhere → rejected.
    sm.onEdgePresent(0);
    auto es = drainEffects(sm);
    const core::Effect* rej = findEffect(es, EffectKind::PLACEMENT_REJECTED);
    CHECK(rej != nullptr);
    if (rej) CHECK_EQ((int)rej->b, (int)RejectReason::ROAD_NOT_CONNECTED);

    // Place settlement at v=5; then road at one of v=5's incident edges → OK.
    const uint8_t v = 5;
    sm.onVertexPresent(v);
    drainEffects(sm);
    uint8_t e = anyEdgeAtVertex(v);
    CHECK(e != 0xFF);
    sm.onEdgePresent(e);
    es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::PLACED_ROAD));
}

static void test_city_requires_own_settlement() {
    g_current_test = "city_requires_own_settlement";
    StateMachine sm;
    bootToPlaying(sm, 2);

    // Advance to PLAYING quickly: each of 2 players places 2 settlements+roads.
    // We fast-forward by directly manipulating game state for this test.
    // Goal: verify the rule engine's city check only.
    RejectReason r_no_settlement = core::rules::validateCity(10, 0);
    CHECK_EQ((int)r_no_settlement, (int)RejectReason::NOT_MY_SETTLEMENT);

    game::placeSettlement(10, 0);
    RejectReason r_ok = core::rules::validateCity(10, 0);
    CHECK_EQ((int)r_ok, (int)RejectReason::NONE);

    // Wrong player:
    RejectReason r_wrong = core::rules::validateCity(10, 1);
    CHECK_EQ((int)r_wrong, (int)RejectReason::NOT_MY_SETTLEMENT);

    // Already upgraded:
    game::upgradeToCity(10);
    RejectReason r_already = core::rules::validateCity(10, 0);
    CHECK_EQ((int)r_already, (int)RejectReason::VERTEX_OCCUPIED);
}

static void test_robber_on_seven() {
    g_current_test = "robber_on_seven";
    StateMachine sm;
    resetScenario(sm);

    // Shortcut: force the FSM into PLAYING with player 0 as current.
    markConnected(2);
    sm.tick(0);
    drainEffects(sm);
    game::setPhase(GamePhase::PLAYING);
    game::setCurrentPlayer(0);
    game::clearDice();
    // Pin an initial robber tile so we can move it.
    game::setRobberTile(0);

    // Force dice to roll a 7 by seeding; if the seed doesn't roll 7 we
    // re-seed and retry a bounded number of times.
    bool saw_robber = false;
    for (uint32_t s = 1; s < 200 && !saw_robber; ++s) {
        core::rng::seed(s);
        game::clearDice();
        game::setPhase(GamePhase::PLAYING);
        sm.handlePlayerAction(0, ActionKind::ROLL_DICE);
        sm.tick((uint32_t)s);
        auto es = drainEffects(sm);
        const core::Effect* d = findEffect(es, EffectKind::DICE_ROLLED);
        if (d && d->c == 1) saw_robber = true;
    }
    CHECK(saw_robber);
    CHECK_EQ((int)game::phase(), (int)GamePhase::ROBBER);

    // Now move the robber to tile 3.
    sm.onTilePresent(3);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::ROBBER_MOVED));
    CHECK_EQ((int)game::robberTile(), 3);
    CHECK_EQ((int)game::phase(), (int)GamePhase::PLAYING);
}

static void test_winner_triggers_game_over() {
    g_current_test = "winner_triggers_game_over";
    StateMachine sm;
    resetScenario(sm);
    markConnected(2);
    sm.tick(0);
    drainEffects(sm);

    game::setPhase(GamePhase::PLAYING);
    game::setCurrentPlayer(0);
    game::setHasRolled(true);
    // 4 cities = 8 VP + 1 settlement = 1 VP + 1 VP dev card = 10 VP.
    // Use vertices spaced apart from each other.
    game::placeSettlement(0, 0); game::upgradeToCity(0);
    game::placeSettlement(8, 0); game::upgradeToCity(8);
    game::placeSettlement(16, 0); game::upgradeToCity(16);
    game::placeSettlement(24, 0); game::upgradeToCity(24);
    game::placeSettlement(32, 0);
    game::setDevCardCount(0, Dev::VP, 1);

    // END_TURN forces recompute / winner check.
    sm.handlePlayerAction(0, ActionKind::END_TURN);
    sm.tick(1);
    auto es = drainEffects(sm);
    const core::Effect* w = findEffect(es, EffectKind::WINNER);
    CHECK(w != nullptr);
    if (w) CHECK_EQ((int)w->a, 0);
    CHECK_EQ((int)game::phase(), (int)GamePhase::GAME_OVER);
}

static void test_robber_same_tile_rejected() {
    g_current_test = "robber_same_tile_rejected";
    game::init();
    game::setRobberTile(5);
    RejectReason r = core::rules::validateRobberMove(5);
    CHECK_EQ((int)r, (int)RejectReason::ROBBER_SAME_TILE);
    RejectReason ok = core::rules::validateRobberMove(7);
    CHECK_EQ((int)ok, (int)RejectReason::NONE);
}

static void test_road_occupied_rejected() {
    g_current_test = "road_occupied_rejected";
    game::init();
    game::placeSettlement(VERTEX_TOPO[0].id, 0);
    game::placeRoad(0, 0);
    RejectReason r = core::rules::validateRoad(0, 1, /*initial=*/false);
    CHECK_EQ((int)r, (int)RejectReason::ROAD_OCCUPIED);
}

// Verify the sensor-driven city upgrade path: settlement removed (onVertexAbsent)
// then piece placed back (onVertexPresent) → PLACED_CITY emitted and game state
// reflects the city.
static void test_city_upgrade_via_sensor() {
    g_current_test = "city_upgrade_via_sensor";
    StateMachine sm;
    resetScenario(sm);
    markConnected(2);

    game::setPhase(GamePhase::PLAYING);
    game::setCurrentPlayer(0);
    game::placeSettlement(10, 0);   // settlement already recorded in game state

    // Player has prepaid for a city upgrade.
    game::addPendingCityBuy(0);

    // Player lifts the settlement piece off the board.
    sm.onVertexAbsent(10);
    auto es = drainEffects(sm);
    CHECK(!containsEffect(es, EffectKind::PLACED_CITY));   // no upgrade yet
    CHECK(!game::vertexState(10).is_city);                 // still a settlement

    // Player sets the city piece down on the same vertex.
    sm.onVertexPresent(10);
    es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::PLACED_CITY));
    CHECK(game::vertexState(10).is_city);
    CHECK_EQ((int)game::vertexState(10).owner, 0);
}

// Verify that a city upgrade does NOT fire when no prior removal was detected
// (guards against false upgrades from sensor glitches on a stationary piece).
static void test_city_upgrade_requires_prior_removal() {
    g_current_test = "city_upgrade_requires_prior_removal";
    StateMachine sm;
    resetScenario(sm);
    markConnected(2);

    game::setPhase(GamePhase::PLAYING);
    game::setCurrentPlayer(0);
    game::placeSettlement(10, 0);

    // Fire onVertexPresent without a prior onVertexAbsent (sensor glitch).
    sm.onVertexPresent(10);
    auto es = drainEffects(sm);
    CHECK(!containsEffect(es, EffectKind::PLACED_CITY));
    CHECK(!game::vertexState(10).is_city);   // settlement must remain unchanged
}

// Verify that a removal during a different player's turn does not count as a
// pending city upgrade for the piece owner.
static void test_city_upgrade_only_on_owners_turn() {
    g_current_test = "city_upgrade_only_on_owners_turn";
    StateMachine sm;
    resetScenario(sm);
    markConnected(2);

    game::setPhase(GamePhase::PLAYING);
    game::placeSettlement(10, 0);   // settlement belongs to P0

    // It is currently P1's turn.
    game::setCurrentPlayer(1);

    // P0's settlement is removed (perhaps bumped).
    sm.onVertexAbsent(10);

    // P0's settlement is placed back — must NOT upgrade because it is not P0's turn.
    sm.onVertexPresent(10);
    auto es = drainEffects(sm);
    CHECK(!containsEffect(es, EffectKind::PLACED_CITY));
    CHECK(!game::vertexState(10).is_city);
}

// =============================================================================
// New-system scenarios
// =============================================================================

// Helper: enter PLAYING with player 0 active and rolled.
static void enterPlayingWithRoll(StateMachine& sm, uint8_t num = 2) {
    resetScenario(sm);
    markConnected(num);
    sm.tick(0);
    drainEffects(sm);
    game::initDevDeck();
    game::setPhase(GamePhase::PLAYING);
    game::setCurrentPlayer(0);
    game::setHasRolled(true);
}

static void test_purchase_road_consumed_on_placement() {
    g_current_test = "purchase_road_consumed_on_placement";
    StateMachine sm;
    enterPlayingWithRoll(sm);

    // Player 0 has settlement at vertex 0 with adjacent edges.
    game::placeSettlement(0, 0);
    game::addRes(0, Res::LUMBER, 1);
    game::addRes(0, Res::BRICK, 1);

    sm.handlePlayerAction(0, ActionKind::BUY_ROAD);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::PURCHASE_MADE));
    CHECK_EQ((int)game::pendingRoadBuy(0), 1);
    CHECK_EQ((int)game::resCount(0, Res::LUMBER), 0);
    CHECK_EQ((int)game::resCount(0, Res::BRICK), 0);

    // Place road at an edge incident to vertex 0.
    uint8_t e = anyEdgeAtVertex(0);
    sm.onEdgePresent(e);
    es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::PLACED_ROAD));
    CHECK_EQ((int)game::pendingRoadBuy(0), 0);
}

static void test_purchase_insufficient_resources() {
    g_current_test = "purchase_insufficient_resources";
    StateMachine sm;
    enterPlayingWithRoll(sm);

    sm.handlePlayerAction(0, ActionKind::BUY_ROAD);
    auto es = drainEffects(sm);
    const core::Effect* rej = findEffect(es, EffectKind::PLACEMENT_REJECTED);
    CHECK(rej != nullptr);
    if (rej) CHECK_EQ((int)rej->b, (int)RejectReason::INSUFFICIENT_RESOURCES);
    CHECK_EQ((int)game::pendingRoadBuy(0), 0);
}

static void test_distribute_resources_on_roll() {
    g_current_test = "distribute_resources_on_roll";
    game::init();
    markConnected(2);
    // Pin tile 0 to a specific biome+number.
    g_tile_state[0].biome = Biome::FOREST;
    g_tile_state[0].number = 6;
    // Place P0's settlement at the first vertex of tile 0.
    uint8_t v = TILE_TOPO[0].vertices[0];
    game::placeSettlement(v, 0);
    // Move robber away.
    game::setRobberTile(5);
    uint8_t before = game::resCount(0, Res::LUMBER);
    uint8_t dealt = game::distributeResources(6);
    CHECK(dealt >= 1);
    CHECK(game::resCount(0, Res::LUMBER) > before);
}

static void test_distribute_skipped_on_robber_tile() {
    g_current_test = "distribute_skipped_on_robber_tile";
    game::init();
    markConnected(2);
    g_tile_state[0].biome = Biome::FOREST;
    g_tile_state[0].number = 8;
    uint8_t v = TILE_TOPO[0].vertices[0];
    game::placeSettlement(v, 0);
    game::setRobberTile(0);   // robber on the producing tile
    uint8_t before = game::resCount(0, Res::LUMBER);
    uint8_t dealt = game::distributeResources(8);
    (void)dealt;
    CHECK_EQ((int)game::resCount(0, Res::LUMBER), (int)before);
}

static void test_bank_depletion() {
    g_current_test = "bank_depletion";
    game::init();
    markConnected(2);
    // Two players claim more lumber than the bank holds.
    for (uint8_t i = 0; i < 19; ++i) {
        // Configure tile 0 to produce lumber on roll 5.
        g_tile_state[0].biome = Biome::FOREST;
        g_tile_state[0].number = 5;
    }
    // Both players have a city on tile 0 (two cities = 4 lumber/roll).
    uint8_t v0 = TILE_TOPO[0].vertices[0];
    uint8_t v1 = TILE_TOPO[0].vertices[2];   // skip neighbour for distance rule
    game::placeSettlement(v0, 0); game::upgradeToCity(v0);
    game::placeSettlement(v1, 1); game::upgradeToCity(v1);
    // Drain the bank to 3 lumber.
    game::setBankSupply(Res::LUMBER, 3);
    game::setRobberTile(5);
    game::distributeResources(5);
    // Demand is 4 (2 cities × 2), bank has 3 → no one gets any.
    CHECK_EQ((int)game::resCount(0, Res::LUMBER), 0);
    CHECK_EQ((int)game::resCount(1, Res::LUMBER), 0);
    CHECK_EQ((int)game::bankSupply(Res::LUMBER), 3);
}

static void test_seven_triggers_discard() {
    g_current_test = "seven_triggers_discard";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    game::setHasRolled(false);   // we'll re-roll from scratch

    // Give P1 enough cards to require discard.
    for (uint8_t i = 0; i < 8; ++i) game::addRes(1, Res::WOOL, 1);
    CHECK(game::totalCards(1) > 7);

    // Force a 7 by seeding deterministically.
    bool entered_discard = false;
    for (uint32_t s = 1; s < 200 && !entered_discard; ++s) {
        core::rng::seed(s);
        game::clearDice();
        game::setPhase(GamePhase::PLAYING);
        sm.handlePlayerAction(0, ActionKind::ROLL_DICE);
        sm.tick((uint32_t)s);
        drainEffects(sm);
        if (game::phase() == GamePhase::DISCARD) entered_discard = true;
    }
    CHECK(entered_discard);
    CHECK(game::discardRequiredMask() & (1u << 1));
    CHECK_EQ((int)game::discardRequiredCount(1), 4);

    // P1 sends a valid discard.
    core::ActionPayload p;
    p.res[1] = 4;   // discard 4 wool
    sm.handlePlayerAction(1, ActionKind::DISCARD, p);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::DISCARD_COMPLETED));
    CHECK_EQ((int)game::resCount(1, Res::WOOL), 4);
    CHECK_EQ((int)game::phase(), (int)GamePhase::ROBBER);
}

static void test_dev_card_bought_this_turn() {
    g_current_test = "dev_card_bought_this_turn";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    game::initDevDeck();

    // Give P0 the resources for one dev card.
    game::addRes(0, Res::WOOL, 1);
    game::addRes(0, Res::GRAIN, 1);
    game::addRes(0, Res::ORE, 1);

    sm.handlePlayerAction(0, ActionKind::BUY_DEV_CARD);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::DEV_CARD_DRAWN));

    // Find which dev type was drawn.
    Dev drawn = Dev::COUNT;
    for (uint8_t d = 0; d < (uint8_t)Dev::COUNT; ++d) {
        if (game::devCardCount(0, (Dev)d) > 0) {
            drawn = (Dev)d;
            break;
        }
    }
    CHECK((int)drawn != (int)Dev::COUNT);
    if (drawn != Dev::VP) {
        // Should not be playable this turn.
        CHECK(!game::canPlayDevCard(0, drawn));
    }
}

static void test_play_monopoly() {
    g_current_test = "play_monopoly";
    StateMachine sm;
    enterPlayingWithRoll(sm, 3);

    // P0 owns a Monopoly card, not bought this turn.
    game::setDevCardCount(0, Dev::MONOPOLY, 1);
    // P1, P2 each have wool.
    game::addRes(1, Res::WOOL, 3);
    game::addRes(2, Res::WOOL, 5);

    core::ActionPayload p;
    p.monopoly_res = (uint8_t)Res::WOOL;
    sm.handlePlayerAction(0, ActionKind::PLAY_MONOPOLY, p);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::MONOPOLY_PLAYED));
    CHECK_EQ((int)game::resCount(0, Res::WOOL), 8);
    CHECK_EQ((int)game::resCount(1, Res::WOOL), 0);
    CHECK_EQ((int)game::resCount(2, Res::WOOL), 0);
    CHECK_EQ((int)game::devCardCount(0, Dev::MONOPOLY), 0);
    CHECK(game::cardPlayedThisTurn());
}

static void test_play_year_of_plenty() {
    g_current_test = "play_year_of_plenty";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    game::setDevCardCount(0, Dev::YEAR_OF_PLENTY, 1);
    core::ActionPayload p;
    p.card_res_1 = (uint8_t)Res::ORE;
    p.card_res_2 = (uint8_t)Res::GRAIN;
    sm.handlePlayerAction(0, ActionKind::PLAY_YEAR_OF_PLENTY, p);
    drainEffects(sm);
    CHECK_EQ((int)game::resCount(0, Res::ORE), 1);
    CHECK_EQ((int)game::resCount(0, Res::GRAIN), 1);
}

static void test_knight_largest_army() {
    g_current_test = "knight_largest_army";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    // P0 plays 3 knights (over multiple turns, but for the test just call the
    // play function 3 times with refilled cards).
    for (int i = 0; i < 3; ++i) {
        game::setDevCardCount(0, Dev::KNIGHT, 1);
        game::setCardPlayedThisTurn(false);
        // Robber must allow movement; pick a non-current tile.
        game::setRobberTile(0);
        sm.handlePlayerAction(0, ActionKind::PLAY_KNIGHT);
        drainEffects(sm);
        // After PLAY_KNIGHT, phase is ROBBER. Move robber to wrap up.
        if (game::phase() == GamePhase::ROBBER) {
            sm.onTilePresent((uint8_t)((i + 1) % TILE_COUNT));
            drainEffects(sm);
        }
    }
    CHECK_EQ((int)game::knightsPlayed(0), 3);
    CHECK_EQ((int)game::largestArmyPlayer(), 0);
}

static void test_bank_trade_4_to_1() {
    g_current_test = "bank_trade_4_to_1";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    game::addRes(0, Res::LUMBER, 4);
    core::ActionPayload p;
    p.res[(int)Res::LUMBER]  = 4;
    p.want[(int)Res::ORE]    = 1;
    sm.handlePlayerAction(0, ActionKind::BANK_TRADE, p);
    drainEffects(sm);
    CHECK_EQ((int)game::resCount(0, Res::LUMBER), 0);
    CHECK_EQ((int)game::resCount(0, Res::ORE), 1);
}

static void test_bank_trade_invalid_rate() {
    g_current_test = "bank_trade_invalid_rate";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    game::addRes(0, Res::LUMBER, 3);
    core::ActionPayload p;
    p.res[(int)Res::LUMBER] = 3;   // 3 lumber but no port — invalid
    p.want[(int)Res::ORE]   = 1;
    sm.handlePlayerAction(0, ActionKind::BANK_TRADE, p);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::PLACEMENT_REJECTED));
    CHECK_EQ((int)game::resCount(0, Res::LUMBER), 3);
}

static void test_p2p_trade() {
    g_current_test = "p2p_trade";
    StateMachine sm;
    enterPlayingWithRoll(sm);
    game::addRes(0, Res::LUMBER, 2);
    game::addRes(1, Res::ORE, 2);

    core::ActionPayload offer;
    offer.target = 1;
    offer.res[(int)Res::LUMBER] = 2;
    offer.want[(int)Res::ORE]   = 2;
    sm.handlePlayerAction(0, ActionKind::TRADE_OFFER, offer);
    auto es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::TRADE_OFFERED));

    sm.handlePlayerAction(1, ActionKind::TRADE_ACCEPT);
    es = drainEffects(sm);
    CHECK(containsEffect(es, EffectKind::TRADE_ACCEPTED));
    CHECK_EQ((int)game::resCount(0, Res::LUMBER), 0);
    CHECK_EQ((int)game::resCount(0, Res::ORE), 2);
    CHECK_EQ((int)game::resCount(1, Res::ORE), 0);
    CHECK_EQ((int)game::resCount(1, Res::LUMBER), 2);
    CHECK(!game::hasPendingTrade());
}

static void test_longest_road() {
    g_current_test = "longest_road";
    game::init();
    game::setNumPlayers(2);

    // Build a chain of 5 connected roads for P0.
    // We pick edges sharing endpoints by walking VERTEX_TOPO.
    // Start from vertex 0; pick one of its edges, then walk.
    uint8_t cur_v = 0;
    uint8_t prev_e = 0xFF;
    for (uint8_t step = 0; step < 5; ++step) {
        const VertexDef& vd = VERTEX_TOPO[cur_v];
        uint8_t e = 0xFF;
        for (uint8_t i = 0; i < 3; ++i) {
            uint8_t cand = vd.edges[i];
            if (cand >= EDGE_COUNT) continue;
            if (cand == prev_e) continue;
            if (game::edgeState(cand).owner != NO_PLAYER) continue;
            e = cand; break;
        }
        if (e == 0xFF) break;
        game::placeRoad(e, 0);
        const EdgeDef& ed = EDGE_TOPO[e];
        uint8_t other = (ed.vertices[0] == cur_v) ? ed.vertices[1] : ed.vertices[0];
        cur_v = other;
        prev_e = e;
    }
    CHECK_EQ((int)game::roadCount(0), 5);
    game::recomputeLongestRoad();
    CHECK_EQ((int)game::longestRoadPlayer(), 0);
    CHECK(game::longestRoadLength() >= 5);
}

// =============================================================================
// main
// =============================================================================
int main() {
    std::printf("Catan FSM simulation\n");
    std::printf("--------------------\n");

    test_lobby_to_setup();
    test_full_number_reveal();
    test_distance_rule();
    test_initial_road_must_touch_settlement();
    test_city_requires_own_settlement();
    test_robber_on_seven();
    test_winner_triggers_game_over();
    test_robber_same_tile_rejected();
    test_road_occupied_rejected();
    test_city_upgrade_via_sensor();
    test_city_upgrade_requires_prior_removal();
    test_city_upgrade_only_on_owners_turn();

    test_purchase_road_consumed_on_placement();
    test_purchase_insufficient_resources();
    test_distribute_resources_on_roll();
    test_distribute_skipped_on_robber_tile();
    test_bank_depletion();
    test_seven_triggers_discard();
    test_dev_card_bought_this_turn();
    test_play_monopoly();
    test_play_year_of_plenty();
    test_knight_largest_army();
    test_bank_trade_4_to_1();
    test_bank_trade_invalid_rate();
    test_p2p_trade();
    test_longest_road();

    std::printf("--------------------\n");
    std::printf("Checks: %d  Failed: %d\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
