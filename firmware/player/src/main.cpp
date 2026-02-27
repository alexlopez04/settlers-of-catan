#include <Arduino.h>
#include "catan.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#define SERIAL_BAUD 115200
#define PLAYER_ID 1  // Change per station

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { ; }

    Serial.println("Player station initialized!");
}

void loop() {
    // TODO: Add logic here

    delay(100);
}