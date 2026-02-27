#include <Arduino.h>
#include "catan.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#define SERIAL_BAUD 115200

// Simple state
uint8_t connected_players = 0;
uint32_t current_tile_index = 0;
_catan_Phase game_phase = catan_Phase_WAITING_FOR_PLAYERS;

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { ; }  // Wait for serial

    Serial.println("Central board initialized!");
}

void loop() {
    // TODO: Add logic here

    delay(50);
}