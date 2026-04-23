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
    // Player 0 reports VP_TO_WIN via an ACTION_REPORT.
    sm.handlePlayerAction(0, ActionKind::REPORT, VP_TO_WIN);
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

    std::printf("--------------------\n");
    std::printf("Checks: %d  Failed: %d\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
