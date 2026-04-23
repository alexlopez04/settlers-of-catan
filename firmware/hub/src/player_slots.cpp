// =============================================================================
// player_slots.cpp — see header.
//
// The in-memory map is session-scoped: it is cleared on every power cycle so
// player numbers are never carried over from a previous game.  Within a
// session, the table survives brief BLE disconnects so a reconnecting device
// returns to the same seat without NVS involvement.
// =============================================================================

#include "player_slots.h"
#include "catan_log.h"

#include <string.h>

namespace {

slots::Slot g_table[MAX_PLAYERS];

}  // namespace

namespace slots {

void init() {
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        g_table[i].occupied       = false;
        g_table[i].conn_handle    = NO_CONN;
        g_table[i].client_id[0]   = '\0';
    }
}

uint8_t lookup(const char* client_id) {
    if (!client_id || !client_id[0]) return NO_SLOT;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (strncmp(g_table[i].client_id, client_id, CLIENT_ID_MAX) == 0) {
            return i;
        }
    }
    return NO_SLOT;
}

uint8_t claim(const char* client_id, uint16_t conn) {
    if (!client_id || !client_id[0]) return NO_SLOT;

    // 1) Restore any prior binding for this client_id.
    uint8_t slot = lookup(client_id);

    // 2) Otherwise, take the lowest unbound seat (no client_id stored).
    if (slot == NO_SLOT) {
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (g_table[i].client_id[0] == '\0') { slot = i; break; }
        }
    }

    // 3) Otherwise, recycle the lowest seat that nobody is currently using.
    if (slot == NO_SLOT) {
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (!g_table[i].occupied) { slot = i; break; }
        }
    }

    if (slot == NO_SLOT) {
        LOGW("SLOT", "no free seat for client '%s'", client_id);
        return NO_SLOT;
    }

    // If another conn was holding this slot (same client reconnecting from
    // a new BLE mega_link before the old one timed out), evict it.
    if (g_table[slot].occupied && g_table[slot].conn_handle != conn) {
        LOGW("SLOT", "evicting old conn=%u from slot %u",
             (unsigned)g_table[slot].conn_handle, (unsigned)slot);
    }

    bool changed = strncmp(g_table[slot].client_id, client_id, CLIENT_ID_MAX) != 0;
    if (changed) {
        strncpy(g_table[slot].client_id, client_id, CLIENT_ID_MAX - 1);
        g_table[slot].client_id[CLIENT_ID_MAX - 1] = '\0';
    }
    g_table[slot].occupied    = true;
    g_table[slot].conn_handle = conn;
    LOGI("SLOT", "slot %u <- conn=%u client='%s'%s",
         (unsigned)slot, (unsigned)conn, g_table[slot].client_id,
         changed ? " (new binding)" : "");
    return slot;
}

uint8_t release(uint16_t conn) {
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (g_table[i].occupied && g_table[i].conn_handle == conn) {
            g_table[i].occupied    = false;
            g_table[i].conn_handle = NO_CONN;
            LOGI("SLOT", "slot %u released (conn=%u)",
                 (unsigned)i, (unsigned)conn);
            return i;
        }
    }
    return NO_SLOT;
}

uint8_t slotForConn(uint16_t conn) {
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (g_table[i].occupied && g_table[i].conn_handle == conn) return i;
    }
    return NO_SLOT;
}

const Slot* table() { return g_table; }

uint8_t connectedMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (g_table[i].occupied) m |= (1u << i);
    return m;
}

uint8_t connectedCount() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (g_table[i].occupied) ++n;
    return n;
}

}  // namespace slots
