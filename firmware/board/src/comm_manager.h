#pragma once
// =============================================================================
// comm_manager.h — I2C protobuf communication with ESP32 player stations.
//
// The Arduino Mega acts as I2C master.  Each player station (ESP32) is an
// I2C slave at address PLAYER_I2C_BASE + player_id (0x10–0x13).
//
// Master write: sends BoardToPlayer protobuf to a player.
// Master read:  requests PlayerToBoard protobuf from a player.
// =============================================================================

#include <stdint.h>
#include "proto/catan.pb.h"

namespace comm {

void init();

// Detect which player stations are physically connected on the I2C bus.
// Returns a bitmask: bit 0 = player 0, bit 1 = player 1, etc.
uint8_t detectPlayers();

// Send a BoardToPlayer message to a specific player station.
// Returns true if the I2C transmission succeeded.
bool sendToPlayer(uint8_t player_id, const catan_BoardToPlayer& msg);

// Read a PlayerToBoard message from a specific player station.
// Returns true if a valid message was received.
bool readFromPlayer(uint8_t player_id, catan_PlayerToBoard& msg);

// Broadcast a BoardToPlayer message to all connected players.
void broadcastToAll(uint8_t connected_mask, const catan_BoardToPlayer& base_msg);

// Build and send per-player state messages (each player gets their own ID/resources).
void syncStateToAll(uint8_t connected_mask);

}  // namespace comm
