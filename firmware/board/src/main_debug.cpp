// =============================================================================
// main_debug.cpp — I2C expander input monitor for pin-mapping verification.
//
// Compiled only when CATAN_DEBUG_MONITOR is defined (see platformio.ini
// env:debug_monitor).  All game logic is excluded — this firmware simply
// reads all eight PCF8575 expanders every POLL_MS milliseconds and logs
// any bit that changes state.
//
// Output format (Serial @ 115200):
//   [CHANGE] exp=0x22 bit=3 state=1   <- bit went HIGH (sensor released)
//   [CHANGE] exp=0x22 bit=3 state=0   <- bit went LOW  (sensor triggered)
//
// Expander address mapping (from config.h):
//   idx 0 → 0x20   idx 1 → 0x21   idx 2 → 0x22   idx 3 → 0x23
//   idx 4 → 0x24   idx 5 → 0x25   idx 6 → 0x26   idx 7 → 0x27
//
// Use this to physically identify which expander/bit each sensor corresponds
// to, then fill in pin_map.cpp accordingly.
// =============================================================================

#ifdef CATAN_DEBUG_MONITOR

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

static constexpr uint32_t POLL_MS = 50;  // 20 Hz — fast enough to catch brief contacts

// Last-known state for each expander word (initialised to 0xFFFF = all high/idle).
static uint16_t prev[EXPANDER_COUNT];
static uint16_t curr[EXPANDER_COUNT];

// Returns the raw 16-bit word from the expander (low byte first), or 0xFFFF on error.
static uint16_t readExpander(uint8_t idx) {
    Wire.requestFrom(EXPANDER_ADDRS[idx], (uint8_t)2);
    if (Wire.available() >= 2) {
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        return (uint16_t)lo | ((uint16_t)hi << 8);
    }
    return 0xFFFF;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { /* wait for USB CDC on Leonardo-class boards */ }

    Serial.println(F("=== Catan I2C Expander Monitor ==="));
    Serial.println(F("Polling all 8 PCF8575 expanders. Bit LOW = sensor active."));
    Serial.println(F("Format: [CHANGE] exp=0xAA bit=B  LOW (triggered)"));
    Serial.println(F("        [CHANGE] exp=0xAA bit=B HIGH (released)"));
    Serial.println();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_BUS_HZ);

    // Set all expander pins to input with pull-ups by writing 0xFFFF.
    // Quasi-bidirectional I/O: writing 1 enables the weak pull-up and
    // allows the pin to be driven low externally (input mode).
    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        Wire.beginTransmission(EXPANDER_ADDRS[i]);
        Wire.write(0xFF);  // low byte (P0–P7)
        Wire.write(0xFF);  // high byte (P10–P17)
        Wire.endTransmission();
    }

    // Probe which expanders are present and log their initial state.
    Serial.println(F("[INIT] Scanning expanders..."));
    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        prev[i] = readExpander(i);
        // A missing expander will NAK and readExpander returns 0xFFFF.
        Serial.print(F("[INIT] exp=0x"));
        if (EXPANDER_ADDRS[i] < 0x10) Serial.print('0');
        Serial.print(EXPANDER_ADDRS[i], HEX);
        if (prev[i] == 0xFFFF) {
            Serial.println(F("  0xFFFF (no ack or all-high)"));
        } else {
            Serial.print(F("  0x"));
            if (prev[i] < 0x1000) Serial.print('0');
            if (prev[i] < 0x100)  Serial.print('0');
            if (prev[i] < 0x10)   Serial.print('0');
            Serial.println(prev[i], HEX);
        }
    }
    Serial.println(F("[INIT] Done. Watching for changes...\n"));
}

void loop() {
    static uint32_t last_poll_ms = 0;
    uint32_t now = millis();
    if (now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;

    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        curr[i] = readExpander(i);
        uint16_t changed = curr[i] ^ prev[i];
        if (changed == 0) continue;

        // Report each changed bit individually.
        for (uint8_t bit = 0; bit < PINS_PER_EXPANDER; ++bit) {
            if (!(changed & (1u << bit))) continue;
            bool high = (curr[i] >> bit) & 1;

            Serial.print(F("[CHANGE] exp=0x"));
            if (EXPANDER_ADDRS[i] < 0x10) Serial.print('0');
            Serial.print(EXPANDER_ADDRS[i], HEX);
            Serial.print(F("  bit="));
            Serial.print(bit);
            if (high) {
                Serial.println(F("  HIGH (released)"));
            } else {
                Serial.println(F("  LOW  (triggered)"));
            }
        }

        prev[i] = curr[i];
    }
}

#endif  // CATAN_DEBUG_MONITOR
