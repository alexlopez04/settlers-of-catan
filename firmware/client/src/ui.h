#pragma once
// =============================================================================
// ui.h — Screen management and button-driven navigation for the Catan client.
//
// Navigation model (3 buttons: A = left, B = middle/confirm, C = right):
//
//   Top-level screens (cycle with A/C short-press):
//     GAME_INFO   — phase, current player, dice result, connected count
//     ACTIONS     — list of available actions for this phase; B sends them
//     RESOURCES   — VP + 5 resource counters; B enters edit mode for cursor row
//
//   RESOURCES edit mode:
//     A = decrement  |  C = increment  |  B = confirm & schedule sync
//
// The UI holds a copy of the last BoardState and local resource/VP values.
// Calling ui::onBoardState() updates the game copy and triggers a redraw.
// ui::onSlotAssigned() records which player slot was assigned.
//
// ui::sendAction is a user-supplied callback invoked whenever the player
// selects an action.
// =============================================================================

#include <stdint.h>
#include "input.h"
#include "proto/catan.pb.h"

namespace ui {

// ── Callbacks supplied by main.cpp ───────────────────────────────────────────

// Called when the user triggers a game action.
typedef void (*ActionSender)(catan_PlayerAction action);

// Called when the user's local VP/resource report should be sent.
typedef void (*ReportSender)(uint32_t vp,
                              uint32_t lumber, uint32_t wool,
                              uint32_t grain,  uint32_t brick,
                              uint32_t ore);

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void init(ActionSender action_cb, ReportSender report_cb);

// ── State updates from BLE layer ─────────────────────────────────────────────

// Called when a new BoardState notification arrives.
void onBoardState(const catan_BoardState& state);

// Called once the hub notifies us of our player slot (0..3) or 0xFF if none.
void onSlotAssigned(uint8_t slot);

// Called when BLE connection state changes.
enum class BleStatus : uint8_t { SCANNING, CONNECTING, CONNECTED, DISCONNECTED };
void onBleStatus(BleStatus status);

// ── Input ─────────────────────────────────────────────────────────────────────

// Forward a button event from the input module.
void onButton(const input::Event& evt);

// ── Render ────────────────────────────────────────────────────────────────────

// Redraw the current screen if enough time has passed since the last draw.
// Returns true if a draw was performed.
bool tick();

}  // namespace ui
