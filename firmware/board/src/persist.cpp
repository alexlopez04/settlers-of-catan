// =============================================================================
// persist.cpp — NVS-backed game state persistence (see persist.h).
// =============================================================================

#include "persist.h"
#include "game_state.h"
#include "board_topology.h"
#include "comms.h"
#include "catan_log.h"

#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>

namespace {
constexpr char k_namespace[] = "catan";
constexpr char k_key[]       = "save";
}  // namespace

namespace persist {

// ── init ─────────────────────────────────────────────────────────────────
bool init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOGW("PERSIST", "NVS dirty, erasing partition");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        LOGE("PERSIST", "nvs_flash_init failed: %d", (int)err);
        return false;
    }
    return true;
}

// ── hasSavedGame ─────────────────────────────────────────────────────────
bool hasSavedGame() {
    nvs_handle_t h;
    if (nvs_open(k_namespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, k_key, nullptr, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(SavedGame)) return false;
    // Also verify the version byte.
    SavedGame tmp;
    return load(tmp);
}

// ── save ─────────────────────────────────────────────────────────────────
bool save() {
    SavedGame sg;
    memset(&sg, 0, sizeof(sg));
    sg.version = SAVED_GAME_VERSION;

    // ── Core state ──────────────────────────────────────────────────────
    sg.phase          = (uint8_t)game::phase();
    sg.num_players    = game::numPlayers();
    sg.current_player = game::currentPlayer();
    sg.robber_tile    = game::robberTile();
    sg.difficulty     = (uint8_t)game::difficulty();

    // ── Dice / per-turn flags ────────────────────────────────────────────
    sg.die1                  = game::lastDie1();
    sg.die2                  = game::lastDie2();
    sg.has_rolled            = game::hasRolled() ? 1 : 0;
    sg.card_played_this_turn = game::cardPlayedThisTurn() ? 1 : 0;

    // ── Number reveal ────────────────────────────────────────────────────
    sg.reveal_index = game::revealIndex();

    // ── Setup ────────────────────────────────────────────────────────────
    sg.setup_round        = game::setupRound();
    sg.setup_turn         = game::setupTurn();
    sg.setup_first_player = game::setupFirstPlayer();

    // ── Bonus holders ────────────────────────────────────────────────────
    sg.largest_army_player = game::largestArmyPlayer();
    sg.longest_road_player = game::longestRoadPlayer();
    sg.longest_road_length = game::longestRoadLength();

    // ── Tile layout ──────────────────────────────────────────────────────
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        sg.tile_biomes[t]  = (uint8_t)g_tile_state[t].biome;
        sg.tile_numbers[t] = g_tile_state[t].number;
    }

    // ── Vertex ownership (nibble-packed) ─────────────────────────────────
    memset(sg.vertex_packed, 0xFF, sizeof(sg.vertex_packed));
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        const VertexState& vs = game::vertexState(v);
        if (vs.owner == NO_PLAYER) continue;
        uint8_t nib = (uint8_t)(vs.owner & 0x3) | (vs.is_city ? 0x4 : 0x0);
        uint8_t bi  = v >> 1;
        if (v & 1) sg.vertex_packed[bi] = (sg.vertex_packed[bi] & 0x0F) | (uint8_t)(nib << 4);
        else        sg.vertex_packed[bi] = (sg.vertex_packed[bi] & 0xF0) | nib;
    }

    // ── Edge ownership (nibble-packed) ───────────────────────────────────
    memset(sg.edge_packed, 0xFF, sizeof(sg.edge_packed));
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        const EdgeState& es = game::edgeState(e);
        if (es.owner == NO_PLAYER) continue;
        uint8_t nib = (uint8_t)(es.owner & 0x3);
        uint8_t bi  = e >> 1;
        if (e & 1) sg.edge_packed[bi] = (sg.edge_packed[bi] & 0x0F) | (uint8_t)(nib << 4);
        else        sg.edge_packed[bi] = (sg.edge_packed[bi] & 0xF0) | nib;
    }

    // ── Resources ────────────────────────────────────────────────────────
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        for (uint8_t r = 0; r < 5; ++r)
            sg.res_count[p][r] = game::resCount(p, (Res)r);
    for (uint8_t r = 0; r < 5; ++r)
        sg.bank_supply[r] = game::bankSupply((Res)r);

    // ── Dev cards ────────────────────────────────────────────────────────
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        for (uint8_t d = 0; d < 5; ++d)
            sg.dev_count[p][d] = game::devCardCount(p, (Dev)d);
    {
        const uint8_t* deck = game::devDeckData();
        if (deck) memcpy(sg.dev_deck, deck, 25);
    }
    sg.dev_deck_pos  = game::devDeckPos();
    sg.dev_deck_size = game::devDeckSizeTotal();

    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        sg.knights_played[p] = game::knightsPlayed(p);

    // ── Pending purchases ────────────────────────────────────────────────
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        sg.pending_road_buy[p]       = game::pendingRoadBuy(p);
        sg.pending_settlement_buy[p] = game::pendingSettlementBuy(p);
        sg.pending_city_buy[p]       = game::pendingCityBuy(p);
        sg.free_roads_remaining[p]   = game::freeRoadsRemaining(p);
    }

    // ── Discard / steal ──────────────────────────────────────────────────
    sg.discard_required_mask  = game::discardRequiredMask();
    sg.steal_eligible_mask    = game::stealEligibleMask();
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        sg.discard_required_count[p] = game::discardRequiredCount(p);

    // ── Player identities ────────────────────────────────────────────────
    comms::getSlotClientIds(sg.client_ids);

    // ── Write to NVS ─────────────────────────────────────────────────────
    nvs_handle_t h;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        LOGE("PERSIST", "nvs_open(RW) failed: %d", (int)err);
        return false;
    }
    err = nvs_set_blob(h, k_key, &sg, sizeof(sg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        LOGE("PERSIST", "nvs_set_blob/commit failed: %d", (int)err);
        return false;
    }
    LOGI("PERSIST", "saved (%u B, phase=%u player=%u/%u)",
         (unsigned)sizeof(sg), (unsigned)sg.phase,
         (unsigned)sg.current_player, (unsigned)sg.num_players);
    return true;
}

// ── load ─────────────────────────────────────────────────────────────────
bool load(SavedGame& sg) {
    nvs_handle_t h;
    if (nvs_open(k_namespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(sg);
    esp_err_t err = nvs_get_blob(h, k_key, &sg, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(sg)) return false;
    if (sg.version != SAVED_GAME_VERSION) {
        LOGW("PERSIST", "version mismatch (%u != %u), ignoring",
             (unsigned)sg.version, (unsigned)SAVED_GAME_VERSION);
        return false;
    }
    return true;
}

// ── clear ─────────────────────────────────────────────────────────────────
void clear() {
    nvs_handle_t h;
    if (nvs_open(k_namespace, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, k_key);
    nvs_commit(h);
    nvs_close(h);
    LOGI("PERSIST", "saved game cleared");
}

// ── restore ───────────────────────────────────────────────────────────────
// Applies a SavedGame snapshot to the live game + comms state.
// Caller must have called game::init() and sm.prepareForResume() first.
void restore(const SavedGame& sg) {
    // ── Core state ──────────────────────────────────────────────────────
    game::setPhase((GamePhase)sg.phase);
    game::setNumPlayers(sg.num_players);
    game::setCurrentPlayer(sg.current_player);
    if (sg.robber_tile < TILE_COUNT) game::setRobberTile(sg.robber_tile);
    game::setDifficulty((Difficulty)sg.difficulty);

    // ── Dice / per-turn flags ────────────────────────────────────────────
    game::setHasRolled(sg.has_rolled != 0);
    game::setCardPlayedThisTurn(sg.card_played_this_turn != 0);

    // ── Number reveal ────────────────────────────────────────────────────
    game::setRevealIndex(sg.reveal_index);

    // ── Setup ────────────────────────────────────────────────────────────
    game::setSetupRound(sg.setup_round);
    game::setSetupTurn(sg.setup_turn);
    game::setSetupFirstPlayer(sg.setup_first_player);

    // ── Bonus holders (must be set before recomputeVpCacheOnly) ─────────
    game::setLargestArmyPlayer(sg.largest_army_player);
    game::setLongestRoadPlayer(sg.longest_road_player);
    game::setLongestRoadLength(sg.longest_road_length);

    // ── Tile layout ──────────────────────────────────────────────────────
    for (uint8_t t = 0; t < TILE_COUNT; ++t) {
        g_tile_state[t].biome  = (Biome)sg.tile_biomes[t];
        g_tile_state[t].number = sg.tile_numbers[t];
    }

    // ── Vertex ownership ────────────────────────────────────────────────
    for (uint8_t v = 0; v < VERTEX_COUNT; ++v) {
        uint8_t bi  = v >> 1;
        uint8_t nib = (v & 1) ? (sg.vertex_packed[bi] >> 4) & 0xF
                               : sg.vertex_packed[bi] & 0xF;
        if (nib == 0xF) game::setVertexState(v, NO_PLAYER, false);
        else            game::setVertexState(v, nib & 0x3, (nib & 0x4) != 0);
    }

    // ── Edge ownership ──────────────────────────────────────────────────
    for (uint8_t e = 0; e < EDGE_COUNT; ++e) {
        uint8_t bi  = e >> 1;
        uint8_t nib = (e & 1) ? (sg.edge_packed[bi] >> 4) & 0xF
                               : sg.edge_packed[bi] & 0xF;
        game::setEdgeState(e, (nib == 0xF) ? NO_PLAYER : (nib & 0x3));
    }

    // ── Resources ────────────────────────────────────────────────────────
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        for (uint8_t r = 0; r < 5; ++r)
            game::setResCount(p, (Res)r, sg.res_count[p][r]);
    for (uint8_t r = 0; r < 5; ++r)
        game::setBankSupply((Res)r, sg.bank_supply[r]);

    // ── Dev cards ────────────────────────────────────────────────────────
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        for (uint8_t d = 0; d < 5; ++d)
            game::setDevCardCount(p, (Dev)d, sg.dev_count[p][d]);
    game::restoreDevDeck(sg.dev_deck, sg.dev_deck_pos, sg.dev_deck_size);
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        game::setKnightsPlayed(p, sg.knights_played[p]);

    // ── Pending purchases ────────────────────────────────────────────────
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        game::setPendingRoadBuy(p,       sg.pending_road_buy[p]);
        game::setPendingSettlementBuy(p, sg.pending_settlement_buy[p]);
        game::setPendingCityBuy(p,       sg.pending_city_buy[p]);
        game::setFreeRoadsRemaining(p,   sg.free_roads_remaining[p]);
    }

    // ── Discard / steal ──────────────────────────────────────────────────
    game::clearAllDiscardRequired();
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        if (sg.discard_required_count[p])
            game::setDiscardRequired(p, sg.discard_required_count[p]);
    game::setStealEligibleMask(sg.steal_eligible_mask);

    // ── VP cache — computed from restored piece state + bonus holders ────
    game::recomputeVpCacheOnly();

    // ── Player identities ────────────────────────────────────────────────
    comms::restoreSlotClientIds(sg.client_ids);

    LOGI("PERSIST", "restore done: phase=%s player=%u/%u",
         game::phaseName((GamePhase)sg.phase),
         (unsigned)sg.current_player, (unsigned)sg.num_players);
}

}  // namespace persist
