// =============================================================================
// ui.cpp — Screen rendering and button navigation for the Catan client.
// =============================================================================

#include "ui.h"
#include "display.h"
#include "config.h"
#include "catan_log.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// ── Font aliases ──────────────────────────────────────────────────────────────
// U8g2 font names: https://github.com/olikraus/u8g2/wiki/fntlistall
// u8g2_font_6x10_tr  — 6 px wide, 10 px tall (max ~21 chars/line)
// u8g2_font_5x8_tr   — 5 px wide,  8 px tall (max ~25 chars/line)
// u8g2_font_4x6_tr   — 4 px wide,  6 px tall (tiny labels)
// u8g2_font_10x20_tr — large digits for roll result

// These are declared in the U8g2 library; include the actual header here.
#include <U8g2lib.h>

namespace ui {

// ── Internal types ────────────────────────────────────────────────────────────

enum class Screen : uint8_t {
    GAME_INFO = 0,
    ACTIONS   = 1,
    RESOURCES = 2,
    SCREEN_COUNT,
};

// Resource row indices in the RESOURCES screen
enum class ResRow : uint8_t { VP = 0, LUMBER, WOOL, GRAIN, BRICK, ORE, ROW_COUNT };
static constexpr uint8_t RES_ROW_COUNT = static_cast<uint8_t>(ResRow::ROW_COUNT);

static const char* const RES_LABELS[RES_ROW_COUNT] = {
    "VP", "Lumber", "Wool", "Grain", "Brick", "Ore"
};

static const char* const PHASE_LABELS[] = {
    "Lobby",           // 0
    "Board Setup",     // 1
    "Num Reveal",      // 2
    "Init Placement",  // 3
    "Playing",         // 4
    "Robber",          // 5
    "Game Over",       // 6
};

static const char* phaseName(catan_GamePhase p) {
    const int idx = static_cast<int>(p);
    if (idx >= 0 && idx <= 6) return PHASE_LABELS[idx];
    return "?";
}

// ── Module state ─────────────────────────────────────────────────────────────

namespace {

ActionSender g_action_cb = nullptr;
ReportSender g_report_cb = nullptr;

catan_BoardState g_state   = catan_BoardState_init_zero;
bool             g_has_state = false;
uint8_t          g_my_slot   = 0xFF;
BleStatus        g_ble_status = BleStatus::SCANNING;

// Local mutable values (VP + 5 resources)
uint32_t g_local[RES_ROW_COUNT] = {};  // indexed by ResRow

Screen   g_screen    = Screen::GAME_INFO;
uint8_t  g_action_cursor = 0;   // selected row in ACTIONS screen
uint8_t  g_res_cursor    = 0;   // selected row in RESOURCES screen
bool     g_res_editing   = false;

uint32_t g_last_draw_ms  = 0;
bool     g_dirty         = true;   // force at least one draw

// ── Action list helpers ───────────────────────────────────────────────────────

struct ActionEntry {
    catan_PlayerAction action;
    const char*        label;
    bool               enabled;
};

// Build the relevant action list for the current phase/state.
// Returns the number of entries written.
static uint8_t buildActions(ActionEntry* out, uint8_t cap) {
    if (!g_has_state) return 0;

    const catan_GamePhase phase     = g_state.phase;
    const bool my_turn = (g_my_slot != 0xFF) &&
                         (g_state.current_player == g_my_slot);
    const bool has_rolled = g_state.has_rolled;
    // Count connected players
    uint8_t conn_count = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        if (g_state.connected_mask & (1u << i)) ++conn_count;
    }

    uint8_t n = 0;
    auto push = [&](catan_PlayerAction a, const char* lbl, bool en) {
        if (n < cap) out[n++] = { a, lbl, en };
    };

    switch (phase) {
        case catan_GamePhase_PHASE_LOBBY:
            push(catan_PlayerAction_ACTION_READY,      "Toggle Ready",  true);
            push(catan_PlayerAction_ACTION_START_GAME, "Start Game",    conn_count >= 1);
            break;
        case catan_GamePhase_PHASE_BOARD_SETUP:
            push(catan_PlayerAction_ACTION_NEXT_NUMBER, "Start Reveal", true);
            break;
        case catan_GamePhase_PHASE_NUMBER_REVEAL:
            push(catan_PlayerAction_ACTION_NEXT_NUMBER, "Next Number",  true);
            break;
        case catan_GamePhase_PHASE_INITIAL_PLACEMENT:
            push(catan_PlayerAction_ACTION_PLACE_DONE, "Placement Done", my_turn);
            break;
        case catan_GamePhase_PHASE_PLAYING:
            if (!has_rolled)
                push(catan_PlayerAction_ACTION_ROLL_DICE, "Roll Dice", my_turn);
            else
                push(catan_PlayerAction_ACTION_END_TURN,  "End Turn",  my_turn);
            push(catan_PlayerAction_ACTION_REPORT, "Sync Resources", true);
            break;
        case catan_GamePhase_PHASE_ROBBER:
            push(catan_PlayerAction_ACTION_SKIP_ROBBER, "Skip Robber", my_turn);
            push(catan_PlayerAction_ACTION_REPORT, "Sync Resources", true);
            break;
        case catan_GamePhase_PHASE_GAME_OVER:
            // No further actions
            break;
        default:
            break;
    }
    return n;
}

// Clamp action cursor to valid range.
static void clampActionCursor(uint8_t count) {
    if (count == 0) { g_action_cursor = 0; return; }
    if (g_action_cursor >= count) g_action_cursor = count - 1;
}

// ── Resource/VP helpers ───────────────────────────────────────────────────────

static uint32_t getLocal(ResRow r) { return g_local[static_cast<uint8_t>(r)]; }
static void     setLocal(ResRow r, uint32_t v) {
    g_local[static_cast<uint8_t>(r)] = v;
}
static constexpr uint32_t RES_MAX = 99;
static constexpr uint32_t VP_MAX  = 20;

static uint32_t maxForRow(ResRow r) {
    return (r == ResRow::VP) ? VP_MAX : RES_MAX;
}

// Restore local values from the latest BoardState (called on state update).
static void restoreLocals() {
    if (!g_has_state || g_my_slot >= 4) return;
    const uint8_t p = g_my_slot;
    if (p < g_state.vp_count)         setLocal(ResRow::VP,     g_state.vp[p]);
    if (p < g_state.res_lumber_count)  setLocal(ResRow::LUMBER, g_state.res_lumber[p]);
    if (p < g_state.res_wool_count)    setLocal(ResRow::WOOL,   g_state.res_wool[p]);
    if (p < g_state.res_grain_count)   setLocal(ResRow::GRAIN,  g_state.res_grain[p]);
    if (p < g_state.res_brick_count)   setLocal(ResRow::BRICK,  g_state.res_brick[p]);
    if (p < g_state.res_ore_count)     setLocal(ResRow::ORE,    g_state.res_ore[p]);
}

// ── Screen drawing ────────────────────────────────────────────────────────────

static void drawHeader(const char* title) {
    // Title row (y=10: baseline for 10px tall font)
    display::drawStr(0, 10, title, u8g2_font_6x10_tr);
    display::drawHLine(12);
}

static void drawBleStatus() {
    const char* lbl = "";
    switch (g_ble_status) {
        case BleStatus::SCANNING:      lbl = "Scan.."; break;
        case BleStatus::CONNECTING:    lbl = "Conn.."; break;
        case BleStatus::CONNECTED:     lbl = "";        break;
        case BleStatus::DISCONNECTED:  lbl = "Lost";    break;
    }
    if (lbl[0] != '\0') {
        display::drawStr(0, 10, lbl, u8g2_font_6x10_tr);
        display::drawHLine(12);
    }
}

// ── Screen: GAME_INFO ─────────────────────────────────────────────────────────

static void drawGameInfo() {
    if (g_ble_status != BleStatus::CONNECTED) {
        drawBleStatus();
        return;
    }

    char buf[32];
    // Header: phase name
    drawHeader(phaseName(g_state.phase));

    uint8_t y = 24;
    const uint8_t line_h = 12;

    // Player slot
    if (g_my_slot < 4) {
        snprintf(buf, sizeof(buf), "You: P%u", (unsigned)(g_my_slot + 1));
    } else {
        snprintf(buf, sizeof(buf), "Waiting for slot");
    }
    display::drawStr(0, y, buf, u8g2_font_5x8_tr);
    y += line_h;

    // Current player / turn indicator
    if (g_has_state) {
        const bool my_turn = (g_my_slot < 4) &&
                             (g_state.current_player == g_my_slot);
        snprintf(buf, sizeof(buf), "Cur: P%u %s",
                 (unsigned)(g_state.current_player + 1),
                 my_turn ? "<YOU>" : "");
        display::drawStr(0, y, buf, u8g2_font_5x8_tr);
        y += line_h;

        // Dice
        if (g_state.has_rolled && (g_state.die1 > 0 || g_state.die2 > 0)) {
            snprintf(buf, sizeof(buf), "Roll: %u+%u=%u",
                     (unsigned)g_state.die1,
                     (unsigned)g_state.die2,
                     (unsigned)(g_state.die1 + g_state.die2));
            display::drawStr(0, y, buf, u8g2_font_5x8_tr);
            y += line_h;
        }

        // Number reveal
        if (g_state.phase == catan_GamePhase_PHASE_NUMBER_REVEAL &&
            g_state.reveal_number > 0) {
            snprintf(buf, sizeof(buf), "Reveal: %u", (unsigned)g_state.reveal_number);
            display::drawStr(0, y, buf, u8g2_font_6x10_tr);
            y += line_h;
        }

        // Game over winner
        if (g_state.phase == catan_GamePhase_PHASE_GAME_OVER &&
            g_state.winner_id < 4) {
            snprintf(buf, sizeof(buf), "Winner: P%u!", (unsigned)(g_state.winner_id + 1));
            display::drawStr(0, y, buf, u8g2_font_6x10_tr);
        }
    }

    // Bottom hint
    display::drawStr(0, 63, "A/C:screen  B:action", u8g2_font_4x6_tr);
}

// ── Screen: ACTIONS ───────────────────────────────────────────────────────────

static void drawActions() {
    if (g_ble_status != BleStatus::CONNECTED) {
        drawBleStatus();
        return;
    }

    drawHeader("Actions");

    ActionEntry entries[8];
    const uint8_t count = buildActions(entries, 8);
    clampActionCursor(count);

    if (count == 0) {
        display::drawStr(0, 36, "No actions", u8g2_font_5x8_tr);
        return;
    }

    // Display up to 4 action rows starting from scroll offset
    const uint8_t visible = 4;
    const uint8_t start   = (g_action_cursor >= visible)
                          ? (g_action_cursor - visible + 1) : 0;

    for (uint8_t i = 0; i < visible && (start + i) < count; ++i) {
        const uint8_t idx = start + i;
        const uint8_t y   = 14 + i * 12 + 10;   // baseline

        if (idx == g_action_cursor) {
            // Highlight row
            display::fillRect(0, 14 + i * 12, display::W, 12);
            display::raw().setDrawColor(0);  // invert text on highlight
        } else {
            display::raw().setDrawColor(1);
        }

        char buf[24];
        snprintf(buf, sizeof(buf), "%s%s",
                 entries[idx].label,
                 entries[idx].enabled ? "" : " -");
        display::drawStr(2, y, buf, u8g2_font_5x8_tr);
        display::raw().setDrawColor(1);  // restore
    }

    display::drawStr(0, 63, "A/C:nav  B:send", u8g2_font_4x6_tr);
}

// ── Screen: RESOURCES ────────────────────────────────────────────────────────

static void drawResources() {
    if (g_ble_status != BleStatus::CONNECTED) {
        drawBleStatus();
        return;
    }

    drawHeader(g_res_editing ? "Edit Resources" : "Resources");

    const uint8_t visible = 4;
    const uint8_t start   = (g_res_cursor >= visible)
                          ? (g_res_cursor - visible + 1) : 0;

    for (uint8_t i = 0; i < visible && (start + i) < RES_ROW_COUNT; ++i) {
        const uint8_t    idx = start + i;
        const ResRow     row = static_cast<ResRow>(idx);
        const uint8_t    y   = 14 + i * 12 + 10;

        const bool selected = (idx == g_res_cursor);
        if (selected && g_res_editing) {
            display::fillRect(0, 14 + i * 12, display::W, 12);
            display::raw().setDrawColor(0);
        } else if (selected) {
            // Selection marker without fill
            display::raw().setDrawColor(1);
            display::raw().drawFrame(0, 14 + i * 12, display::W, 12);
        }

        char buf[24];
        snprintf(buf, sizeof(buf), "%-7s %2u",
                 RES_LABELS[idx],
                 (unsigned)getLocal(row));
        display::drawStr(4, y, buf, u8g2_font_5x8_tr);
        display::raw().setDrawColor(1);
    }

    if (g_res_editing) {
        display::drawStr(0, 63, "A:-1  C:+1  B:done", u8g2_font_4x6_tr);
    } else {
        display::drawStr(0, 63, "A/C:nav  B:edit", u8g2_font_4x6_tr);
    }
}

// ── Master draw ───────────────────────────────────────────────────────────────

static void drawFrame() {
    display::beginFrame();

    switch (g_screen) {
        case Screen::GAME_INFO: drawGameInfo(); break;
        case Screen::ACTIONS:   drawActions();  break;
        case Screen::RESOURCES: drawResources(); break;
        default: break;
    }

    display::commitFrame();
}

// ── Button dispatch ───────────────────────────────────────────────────────────

static void handleResourcesButton(const input::Event& evt) {
    if (evt.type == input::EventType::LONG_PRESS) {
        // Long-press A/C on resources screen: navigate screens anyway
        if (evt.button == input::Button::A) {
            const int s = (static_cast<int>(g_screen) - 1 +
                           static_cast<int>(Screen::SCREEN_COUNT)) %
                          static_cast<int>(Screen::SCREEN_COUNT);
            g_screen = static_cast<Screen>(s);
            g_res_editing = false;
        } else if (evt.button == input::Button::C) {
            const int s = (static_cast<int>(g_screen) + 1) %
                          static_cast<int>(Screen::SCREEN_COUNT);
            g_screen = static_cast<Screen>(s);
            g_res_editing = false;
        }
        g_dirty = true;
        return;
    }

    // Short press
    if (g_res_editing) {
        const ResRow row = static_cast<ResRow>(g_res_cursor);
        const uint32_t cur = getLocal(row);
        const uint32_t max = maxForRow(row);

        if (evt.button == input::Button::A) {
            setLocal(row, (cur > 0) ? cur - 1 : 0);
        } else if (evt.button == input::Button::C) {
            setLocal(row, (cur < max) ? cur + 1 : max);
        } else if (evt.button == input::Button::B) {
            // Confirm edit; send report
            g_res_editing = false;
            if (g_report_cb) {
                g_report_cb(
                    getLocal(ResRow::VP),
                    getLocal(ResRow::LUMBER),
                    getLocal(ResRow::WOOL),
                    getLocal(ResRow::GRAIN),
                    getLocal(ResRow::BRICK),
                    getLocal(ResRow::ORE)
                );
            }
        }
    } else {
        if (evt.button == input::Button::A) {
            if (g_res_cursor > 0) --g_res_cursor;
        } else if (evt.button == input::Button::C) {
            if (g_res_cursor + 1 < RES_ROW_COUNT) ++g_res_cursor;
        } else if (evt.button == input::Button::B) {
            g_res_editing = true;
        }
    }
    g_dirty = true;
}

static void handleActionsButton(const input::Event& evt) {
    if (evt.type == input::EventType::LONG_PRESS) {
        if (evt.button == input::Button::A) {
            const int s = (static_cast<int>(g_screen) - 1 +
                           static_cast<int>(Screen::SCREEN_COUNT)) %
                          static_cast<int>(Screen::SCREEN_COUNT);
            g_screen = static_cast<Screen>(s);
        } else if (evt.button == input::Button::C) {
            const int s = (static_cast<int>(g_screen) + 1) %
                          static_cast<int>(Screen::SCREEN_COUNT);
            g_screen = static_cast<Screen>(s);
        }
        g_dirty = true;
        return;
    }

    ActionEntry entries[8];
    const uint8_t count = buildActions(entries, 8);
    clampActionCursor(count);

    if (evt.button == input::Button::A) {
        if (g_action_cursor > 0) --g_action_cursor;
    } else if (evt.button == input::Button::C) {
        if (count > 0 && g_action_cursor + 1 < count) ++g_action_cursor;
    } else if (evt.button == input::Button::B) {
        if (count > 0 && g_action_cb) {
            const ActionEntry& e = entries[g_action_cursor];
            if (e.enabled) {
                if (e.action == catan_PlayerAction_ACTION_REPORT && g_report_cb) {
                    g_report_cb(
                        getLocal(ResRow::VP),
                        getLocal(ResRow::LUMBER),
                        getLocal(ResRow::WOOL),
                        getLocal(ResRow::GRAIN),
                        getLocal(ResRow::BRICK),
                        getLocal(ResRow::ORE)
                    );
                } else {
                    g_action_cb(e.action);
                }
            }
        }
    }
    g_dirty = true;
}

static void handleGameInfoButton(const input::Event& evt) {
    if (evt.type != input::EventType::SHORT_PRESS) return;
    if (evt.button == input::Button::A) {
        const int s = (static_cast<int>(g_screen) - 1 +
                       static_cast<int>(Screen::SCREEN_COUNT)) %
                      static_cast<int>(Screen::SCREEN_COUNT);
        g_screen = static_cast<Screen>(s);
    } else if (evt.button == input::Button::C) {
        const int s = (static_cast<int>(g_screen) + 1) %
                      static_cast<int>(Screen::SCREEN_COUNT);
        g_screen = static_cast<Screen>(s);
    } else if (evt.button == input::Button::B) {
        // Shortcut: jump to ACTIONS
        g_screen = Screen::ACTIONS;
    }
    g_dirty = true;
}

}  // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

void init(ActionSender action_cb, ReportSender report_cb) {
    g_action_cb  = action_cb;
    g_report_cb  = report_cb;
    g_screen     = Screen::GAME_INFO;
    g_my_slot    = 0xFF;
    g_has_state  = false;
    g_ble_status = BleStatus::SCANNING;
    memset(g_local, 0, sizeof(g_local));
    g_dirty = true;
}

void onBoardState(const catan_BoardState& state) {
    const bool first = !g_has_state;
    g_state     = state;
    g_has_state = true;
    if (first) restoreLocals();
    g_dirty = true;
}

void onSlotAssigned(uint8_t slot) {
    if (g_my_slot != slot) {
        g_my_slot = slot;
        restoreLocals();
        g_dirty = true;
        LOGI("UI", "Slot assigned: %u", (unsigned)slot);
    }
}

void onBleStatus(BleStatus status) {
    g_ble_status = status;
    if (status == BleStatus::SCANNING || status == BleStatus::DISCONNECTED) {
        g_has_state = false;
        g_my_slot   = 0xFF;
    }
    g_dirty = true;
}

void onButton(const input::Event& evt) {
    switch (g_screen) {
        case Screen::GAME_INFO: handleGameInfoButton(evt); break;
        case Screen::ACTIONS:   handleActionsButton(evt);  break;
        case Screen::RESOURCES: handleResourcesButton(evt); break;
        default: break;
    }
}

bool tick() {
    const uint32_t now = millis();
    if (!g_dirty && (now - g_last_draw_ms) < DISPLAY_REFRESH_MS) return false;

    drawFrame();
    g_last_draw_ms = now;
    g_dirty = false;
    return true;
}

}  // namespace ui
