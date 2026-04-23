#pragma once
// =============================================================================
// player_slots.h — Session-scoped client_id → seat (0..3) assignment for the hub.
//
// Each mobile device is identified by a string `client_id` (a UUID the app
// generates once and stores locally). On first connection the hub assigns the
// lowest free seat; on reconnect within the same power-on session the same
// seat is restored from the in-memory table. The table is cleared on every
// boot so player numbers are never carried over from a previous game session.
//
// Each seat additionally tracks the BLE conn handle of the currently
// linked device so the input dispatcher can authoritatively map an
// incoming write to a player_id.
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace slots {

constexpr uint8_t  NO_SLOT       = 0xFF;
constexpr uint16_t NO_CONN       = 0xFFFF;
constexpr size_t   CLIENT_ID_MAX = 40;   // matches catan.options

struct Slot {
    bool     occupied;                   // a device is currently linked
    uint16_t conn_handle;                // BLE handle (NO_CONN if vacant)
    char     client_id[CLIENT_ID_MAX];   // empty string if never claimed
};

void init();

// Look up which seat (if any) a particular client_id is bound to. Returns
// NO_SLOT if not previously seen.
uint8_t lookup(const char* client_id);

// Bind `client_id` to `conn` — restoring the previously-assigned seat or
// claiming the lowest free one. Returns the slot, or NO_SLOT if the table
// is full and no prior binding exists.
uint8_t claim(const char* client_id, uint16_t conn);

// Release the slot currently held by `conn`. Returns the freed slot or
// NO_SLOT if `conn` wasn't linked. The client_id remains in the in-memory
// table so the next connection from the same device within this session
// returns to the same seat.
uint8_t release(uint16_t conn);

// Return the slot currently bound to `conn`, or NO_SLOT.
uint8_t slotForConn(uint16_t conn);

// Read-only view of all slots (length = MAX_PLAYERS).
const Slot* table();

// Bitmask of currently-occupied seats (bit i = slot i).
uint8_t connectedMask();

// Number of currently occupied seats.
uint8_t connectedCount();

}  // namespace slots
