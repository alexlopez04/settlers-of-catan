// =============================================================================
// main_debug.cpp — I2C expander input monitor for pin-mapping verification.
//
// Compiled only when CATAN_DEBUG_MONITOR is defined (see platformio.ini
// env:debug_monitor).  All game logic is excluded — this firmware simply
// reads all eight PCF8574 expanders every POLL_MS milliseconds and logs
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

// Last-known state for each expander byte (0xFF = all high/idle).
static uint8_t prev[EXPANDER_COUNT];
static uint8_t curr[EXPANDER_COUNT];
// Bitmask of expanders that ACKed on the last read (bit i = expander i).
static uint8_t present_mask = 0;

// Reads one byte from an expander.
// Sets *acked = true if the device responded, false if it NAKed.
// Returns the byte read, or 0xFF on NAK.
static uint8_t readExpander(uint8_t idx, bool* acked) {
    uint8_t n = Wire.requestFrom(EXPANDER_ADDRS[idx], (uint8_t)1);
    if (n == 1 && Wire.available()) {
        *acked = true;
        return Wire.read();
    }
    // Drain any stale byte just in case.
    while (Wire.available()) Wire.read();
    *acked = false;
    return 0xFF;
}

static void printAddr(uint8_t idx) {
    Serial.print(F("exp=0x"));
    if (EXPANDER_ADDRS[idx] < 0x10) Serial.print('0');
    Serial.print(EXPANDER_ADDRS[idx], HEX);
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) { /* wait for USB CDC on Leonardo-class boards */ }

    Serial.println(F("=== Catan I2C Expander Monitor ==="));
    Serial.println(F("Polling all 8 PCF8574 expanders. Bit LOW = sensor active."));
    Serial.println(F("Format: [CHANGE] exp=0xAA  bit=B  LOW  (triggered)"));
    Serial.println(F("        [CHANGE] exp=0xAA  bit=B  HIGH (released)"));
    Serial.println();

    Wire.begin();
    Wire.setClock(100000UL);  // 100 kHz — matches the rest of the firmware

    // PCF8574: writing 0xFF to all pins puts them in quasi-bidirectional
    // input mode (weak ~100 µA pull-up active).  Without this, any pin that
    // was previously driven LOW by firmware would read as 0 permanently.
    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        Wire.beginTransmission(EXPANDER_ADDRS[i]);
        Wire.write(0xFF);
        Wire.endTransmission();
    }

    // Probe which expanders are present and log their initial state.
    Serial.println(F("[INIT] Scanning expanders..."));
    present_mask = 0;
    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        bool acked = false;
        prev[i] = readExpander(i, &acked);

        Serial.print(F("[INIT] "));
        printAddr(i);
        Serial.print(F("  "));

        if (!acked) {
            Serial.println(F("NOT CONNECTED (no ACK)"));
        } else {
            present_mask |= (1 << i);
            Serial.print(F("OK  inputs=0b"));
            for (int8_t b = 7; b >= 0; --b) {
                Serial.print((prev[i] >> b) & 1);
            }
            Serial.print(F("  (0x"));
            if (prev[i] < 0x10) Serial.print('0');
            Serial.print(prev[i], HEX);
            // Show which bits are already LOW (sensors currently active).
            uint8_t active = (~prev[i]) & 0xFF;
            if (active) {
                Serial.print(F(")  ACTIVE bits:"));
                for (uint8_t b = 0; b < 8; ++b) {
                    if (active & (1 << b)) {
                        Serial.print(' ');
                        Serial.print(b);
                    }
                }
                Serial.println();
            } else {
                Serial.println(F(")  all idle"));
            }
        }
    }

    uint8_t count = __builtin_popcount(present_mask);
    Serial.print(F("\n[INIT] "));
    Serial.print(count);
    Serial.print(F("/"));
    Serial.print((uint8_t)EXPANDER_COUNT);
    Serial.println(F(" expanders present. Watching for changes...\n"));
}

void loop() {
    static uint32_t last_poll_ms = 0;
    uint32_t now = millis();
    if (now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;

    for (uint8_t i = 0; i < EXPANDER_COUNT; ++i) {
        bool acked = false;
        curr[i] = readExpander(i, &acked);

        // Track connect / disconnect events.
        bool was_present = (present_mask >> i) & 1;
        if (acked != was_present) {
            if (acked) {
                present_mask |= (1 << i);
                Serial.print(F("[CONNECT] "));
                printAddr(i);
                Serial.println(F("  now responding"));
            } else {
                present_mask &= ~(1 << i);
                Serial.print(F("[DISCONNECT] "));
                printAddr(i);
                Serial.println(F("  stopped responding (no ACK)"));
                prev[i] = 0xFF;  // Reset so we don't fire spurious change events on reconnect.
                continue;
            }
        }

        if (!acked) continue;  // Expander absent — nothing to diff.

        uint8_t changed = curr[i] ^ prev[i];
        if (changed == 0) { prev[i] = curr[i]; continue; }

        // Report each changed bit individually.
        for (uint8_t bit = 0; bit < PINS_PER_EXPANDER; ++bit) {
            if (!(changed & (1 << bit))) continue;
            bool high = (curr[i] >> bit) & 1;

            Serial.print(F("[CHANGE] "));
            printAddr(i);
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
