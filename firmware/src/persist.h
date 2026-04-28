#pragma once
// =============================================================================
// persist.h — NVS-backed game state persistence for power-cycle resume.
//
// At every TURN_ADVANCED and key PHASE_ENTERED events, the complete game
// state is serialised into a single NVS blob. On the next boot the board
// checks for a saved game and — once the first player connects — broadcasts
// `has_saved_game = true` in the BoardState. Player 0 sends
// ACTION_RESUME_YES or ACTION_RESUME_NO; the board either restores the
// snapshot or discards it.
// =============================================================================

#include <stdint.h>
#include "config.h"

namespace persist {

// Version tag — increment when SavedGame layout changes incompatibly.
static constexpr uint8_t SAVED_GAME_VERSION  = 1;
// Must match comms.cpp CLIENT_ID_MAX (40).
static constexpr uint8_t PERSIST_CID_MAX     = 40;

// ── SavedGame snapshot ─────────────────────────────────────────────────────
// Plain-old-data struct stored as a single NVS blob.
// All booleans are stored as uint8_t (0/1) to avoid alignment surprises.
struct SavedGame {
    uint8_t version;               // must equal SAVED_GAME_VERSION

    // ── Core game state ───────────────────────────────────────────────────
    uint8_t phase;                 // GamePhase cast to uint8_t
    uint8_t num_players;
    uint8_t current_player;
    uint8_t robber_tile;           // 0xFF if unplaced
    uint8_t difficulty;            // Difficulty cast to uint8_t

    // ── Dice / per-turn flags ─────────────────────────────────────────────
    uint8_t die1;
    uint8_t die2;
    uint8_t has_rolled;            // 0 or 1
    uint8_t card_played_this_turn; // 0 or 1

    // ── Number reveal ─────────────────────────────────────────────────────
    uint8_t reveal_index;

    // ── Setup (snake-draft) ───────────────────────────────────────────────
    uint8_t setup_round;
    uint8_t setup_turn;
    uint8_t setup_first_player;

    // ── Longest Road / Largest Army holders ──────────────────────────────
    // Saved explicitly to preserve tie-breaking history on restore.
    uint8_t largest_army_player;   // NO_PLAYER (0xFF) if none
    uint8_t longest_road_player;   // NO_PLAYER (0xFF) if none
    uint8_t longest_road_length;

    // ── Tile layout ───────────────────────────────────────────────────────
    uint8_t tile_biomes[19];       // Biome cast to uint8_t, one per tile
    uint8_t tile_numbers[19];      // 0–12 (0 = desert)

    // ── Vertex ownership (nibble-packed) ──────────────────────────────────
    // low nibble = even vertex, high nibble = odd vertex.
    //   0x0..0x3 = settlement P0..P3   0x4..0x7 = city P0..P3   0xF = empty
    uint8_t vertex_packed[27];     // 54 vertices → 27 bytes

    // ── Edge ownership (nibble-packed) ────────────────────────────────────
    // low nibble = even edge, high nibble = odd edge.
    //   0x0..0x3 = road P0..P3   0xF = empty
    uint8_t edge_packed[36];       // 72 edges → 36 bytes

    // ── Resources ─────────────────────────────────────────────────────────
    uint8_t res_count[4][5];       // [player][Res index 0..4]
    uint8_t bank_supply[5];        // per Res index

    // ── Development cards ─────────────────────────────────────────────────
    uint8_t dev_count[4][5];       // [player][Dev index 0..4]
    uint8_t dev_deck[25];          // full shuffled deck array
    uint8_t dev_deck_pos;          // next-draw index into dev_deck[]
    uint8_t dev_deck_size;         // valid card count (25 when initialised)
    uint8_t knights_played[4];     // per player

    // ── Pending purchases ─────────────────────────────────────────────────
    uint8_t pending_road_buy[4];
    uint8_t pending_settlement_buy[4];
    uint8_t pending_city_buy[4];
    uint8_t free_roads_remaining[4];

    // ── Discard / steal state ─────────────────────────────────────────────
    uint8_t discard_required_mask;
    uint8_t discard_required_count[4];
    uint8_t steal_eligible_mask;

    // ── Player identities ─────────────────────────────────────────────────
    // Stored per slot so that reconnecting phones reclaim their original seat.
    char client_ids[4][PERSIST_CID_MAX];
};

// ── API ───────────────────────────────────────────────────────────────────

// Initialise the NVS flash subsystem. Call once from setup(), before any
// other persist:: call.  Returns true on success.
bool init();

// Returns true if a complete, valid SavedGame is present in NVS.
bool hasSavedGame();

// Capture the current game + comms state and write it to NVS.
// Returns false on encode or NVS write failure.
bool save();

// Read the saved game from NVS into `sg`.
// Returns false if nothing is saved or the stored data is corrupt/stale.
bool load(SavedGame& sg);

// Erase the saved game from NVS.
void clear();

// Apply a SavedGame snapshot to the live game and comms state.
// Must be called from the game task after game::init() and sm.prepareForResume().
// Does NOT trigger FSM events or update LEDs — the caller handles those.
void restore(const SavedGame& sg);

}  // namespace persist
