#pragma once
// =============================================================================
// display_manager.h — I2C character-LCD display helpers.
// =============================================================================

#include <stdint.h>

namespace display {

void init();
void clear();

// Print up to OLED_COLS chars on a row (0-indexed, 8 rows total at text size 1).
void printRow(uint8_t row, const char* text);

// Convenience wrappers.
void showTitle(const char* title);                         // Row 0
void showMessage(const char* line1, const char* line2 = nullptr);  // Row 1–2
void showPlayerTurn(uint8_t player_num, uint8_t total_players);
void showDiceResult(uint8_t die1, uint8_t die2);
void showSetupPrompt(const char* prompt);

// Render the button-action bar at the very bottom of the OLED (y=56-63).
// A separator line is drawn at y=55.  Pass "" or nullptr to leave a
// position empty.  Labels are left-, center-, and right-aligned.
// Keep labels ≤5 chars so they don't overlap.
void showButtonBar(const char* left, const char* center, const char* right);

}  // namespace display
