// =============================================================================
// main.cpp — Settlers of Catan Bridge (Heltec WiFi LoRa 32 V3)
//
// Data flow:
//
//   Mega  --(Serial1 @ 115200 baud)-->  bridge  --LoRa broadcast-->  players
//   players --LoRa uplink-->  bridge  --(Serial1 push)-->  Mega
//
// The bridge keeps:
//   * `last_state_frame`  : most recent BoardState frame from the Mega
//                           (re-broadcast over LoRa at rate-limited cadence).
// PlayerInput frames received from players over LoRa are forwarded
// immediately to the Mega over the serial link (no queuing needed).
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <RadioLib.h>
#include <U8g2lib.h>

#include "config.h"
#include "catan_wire.h"
#include "proto/catan.pb.h"
#include <pb_decode.h>

// ── UART link to Mega ─────────────────────────────────────────────────────
static HardwareSerial MegaSerial(1);   // UART1 on ESP32-S3

// RX state machine — reassembles framed BoardState bytes from Mega.
enum class RxState : uint8_t { HUNT, LEN, BODY };
static uint8_t  serial_rx_buf[CATAN_MAX_FRAME];
static uint8_t  serial_rx_pos    = 0;
static uint8_t  serial_rx_expect = 0;
static RxState  serial_rx_state  = RxState::HUNT;

// ── Radio ───────────────────────────────────────────────────────────────────
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
static volatile bool rx_flag = false;
IRAM_ATTR static void onDio1() { rx_flag = true; }

// ── OLED ────────────────────────────────────────────────────────────────────
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, /* reset */ OLED_RST, /* scl */ OLED_SCL, /* sda */ OLED_SDA);

// ── Latest BoardState frame received from Mega (framed, ready to TX) ────────
static uint8_t last_state_frame[CATAN_MAX_FRAME];
static volatile uint8_t last_state_len = 0;    // 0 = nothing yet
static volatile bool    state_dirty    = false; // true ⇒ needs LoRa TX

// ── I2C input queue: PlayerInput frames waiting for Mega to read ────────────
struct Frame {
    uint8_t len;
    uint8_t data[CATAN_MAX_FRAME];
};
static Frame   input_queue[PLAYER_INPUT_QUEUE];
static volatile uint8_t q_head = 0, q_tail = 0;    // head=write, tail=read

static bool queuePush(const uint8_t* data, uint8_t len) {
    uint8_t next = (uint8_t)((q_head + 1) % PLAYER_INPUT_QUEUE);
    if (next == q_tail) return false;   // full
    Frame& f = input_queue[q_head];
    f.len = (len > CATAN_MAX_FRAME) ? CATAN_MAX_FRAME : len;
    memcpy(f.data, data, f.len);
    q_head = next;
    return true;
}
static bool queuePop(Frame& out) {
    if (q_tail == q_head) return false;
    out = input_queue[q_tail];
    q_tail = (uint8_t)((q_tail + 1) % PLAYER_INPUT_QUEUE);
    return true;
}
static uint8_t queueSize() {
    int s = (int)q_head - (int)q_tail;
    if (s < 0) s += PLAYER_INPUT_QUEUE;
    return (uint8_t)s;
}

// ── Stats for OLED ──────────────────────────────────────────────────────────
static uint32_t stats_rx_total      = 0;
static uint32_t stats_tx_total      = 0;
static uint32_t stats_serial_rx     = 0;   // BoardState frames from Mega
static uint32_t stats_serial_tx     = 0;   // PlayerInput frames forwarded to Mega
static uint32_t last_oled_ms        = 0;
static uint32_t last_tx_ms          = 0;

// ── Display seen BoardState fields (decoded just for OLED summary) ──────────
static struct {
    bool     valid = false;
    uint8_t  phase = 0;
    uint8_t  cur   = 0;
    uint8_t  np    = 0;
    uint8_t  d1    = 0, d2 = 0;
    uint32_t updated_ms = 0;
} summary;

// =============================================================================
// Serial RX — reassemble framed BoardState bytes arriving from the Mega.
// Call processSerialRx() in loop(); it updates last_state_frame on a complete
// valid frame and sets state_dirty for the LoRa broadcast path.
// =============================================================================

static void processSerialRx() {
    while (MegaSerial.available()) {
        uint8_t b = (uint8_t)MegaSerial.read();
        switch (serial_rx_state) {
            case RxState::HUNT:
                if (b == CATAN_WIRE_MAGIC) {
                    serial_rx_buf[0] = b;
                    serial_rx_pos    = 1;
                    serial_rx_state  = RxState::LEN;
                }
                break;
            case RxState::LEN:
                if (b == 0 || b > CATAN_MAX_PAYLOAD) {
                    serial_rx_state = RxState::HUNT;   // invalid length
                } else {
                    serial_rx_buf[1]  = b;
                    serial_rx_expect  = (uint8_t)(CATAN_FRAME_HEADER + b);
                    serial_rx_pos     = CATAN_FRAME_HEADER;
                    serial_rx_state   = RxState::BODY;
                }
                break;
            case RxState::BODY:
                serial_rx_buf[serial_rx_pos++] = b;
                if (serial_rx_pos >= serial_rx_expect) {
                    serial_rx_state = RxState::HUNT;
                    // Commit the frame
                    memcpy(last_state_frame, serial_rx_buf, serial_rx_expect);
                    last_state_len = serial_rx_expect;
                    state_dirty    = true;
                    stats_serial_rx++;
                    Serial.printf("[SERIAL] RX BoardState len=%u (total=%lu)\n",
                                  serial_rx_buf[1], (unsigned long)stats_serial_rx);
                }
                break;
        }
    }
}

// =============================================================================
// I2C slave handlers
// =============================================================================

// =============================================================================
// Helpers
// =============================================================================

static const char* phaseShort(uint8_t p) {
    switch (p) {
        case catan_GamePhase_PHASE_LOBBY:             return "LOBBY";
        case catan_GamePhase_PHASE_BOARD_SETUP:       return "SETUP";
        case catan_GamePhase_PHASE_NUMBER_REVEAL:     return "REVEAL";
        case catan_GamePhase_PHASE_INITIAL_PLACEMENT: return "PLACE";
        case catan_GamePhase_PHASE_PLAYING:           return "PLAY";
        case catan_GamePhase_PHASE_ROBBER:            return "ROBBER";
        case catan_GamePhase_PHASE_GAME_OVER:         return "OVER";
    }
    return "?";
}

static void decodeSummary() {
    // Decode the cached state frame just to populate OLED summary fields.
    if (last_state_len < CATAN_FRAME_HEADER) return;
    uint8_t payload_len = last_state_frame[1];
    if (payload_len == 0 || payload_len > CATAN_MAX_PAYLOAD) return;

    catan_BoardState s = catan_BoardState_init_zero;
    pb_istream_t is = pb_istream_from_buffer(last_state_frame + CATAN_FRAME_HEADER,
                                             payload_len);
    if (pb_decode(&is, catan_BoardState_fields, &s)) {
        summary.valid      = true;
        summary.phase      = (uint8_t)s.phase;
        summary.cur        = (uint8_t)s.current_player;
        summary.np         = (uint8_t)s.num_players;
        summary.d1         = (uint8_t)s.die1;
        summary.d2         = (uint8_t)s.die2;
        summary.updated_ms = millis();
    }
}

static void refreshOled() {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);

    oled.drawStr(0, 10, "Catan Bridge");
    char line[32];

    if (summary.valid) {
        snprintf(line, sizeof(line), "Phase:%s  P%u/%u",
                 phaseShort(summary.phase),
                 summary.cur + 1, summary.np);
        oled.drawStr(0, 24, line);

        if (summary.d1 || summary.d2) {
            snprintf(line, sizeof(line), "Dice: %u + %u = %u",
                     summary.d1, summary.d2, summary.d1 + summary.d2);
            oled.drawStr(0, 36, line);
        }
    } else {
        oled.drawStr(0, 24, "Waiting for Mega..");
    }

    snprintf(line, sizeof(line), "LoRa Tx:%lu Rx:%lu",
             (unsigned long)stats_tx_total, (unsigned long)stats_rx_total);
    oled.drawStr(0, 50, line);
    snprintf(line, sizeof(line), "Ser Rx:%lu Tx:%lu",
             (unsigned long)stats_serial_rx, (unsigned long)stats_serial_tx);
    oled.drawStr(0, 62, line);

    oled.sendBuffer();
}

// =============================================================================
// setup()
// =============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println("====================================");
    Serial.println(" Catan Bridge (Heltec V3) starting");
    Serial.printf (" proto v%u\n", CATAN_PROTO_VERSION);
    Serial.println("====================================");

    // Power on the OLED + LoRa module via VEXT (active low)
    pinMode(OLED_VEXT, OUTPUT);
    digitalWrite(OLED_VEXT, LOW);
    delay(50);

    // OLED reset pulse
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);  delay(20);
    digitalWrite(OLED_RST, HIGH); delay(20);

    Wire.begin(OLED_SDA, OLED_SCL);
    oled.begin();
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 12, "Catan Bridge");
    oled.drawStr(0, 30, "Booting...");
    oled.sendBuffer();

    // ── LoRa ────────────────────────────────────────────────────────────────
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    int rc = radio.begin(LORA_FREQ_MHZ,
                         LORA_BW_KHZ,
                         LORA_SF,
                         LORA_CR,
                         LORA_SYNCWORD,
                         LORA_POWER_DBM,
                         LORA_PREAMBLE_LEN);
    if (rc != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] begin FAILED: %d\n", rc);
        oled.drawStr(0, 48, "LoRa FAIL!");
        oled.sendBuffer();
        while (true) delay(1000);
    }
    Serial.println("[LORA] begin OK");
    radio.setDio1Action(onDio1);
    rc = radio.startReceive();
    if (rc != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] startReceive: %d\n", rc);
    }

    // ── UART link to Mega ─────────────────────────────────────────────
    MegaSerial.begin(MEGA_SERIAL_BAUD, SERIAL_8N1, MEGA_SERIAL_RX, MEGA_SERIAL_TX);
    Serial.printf("[SERIAL] Mega link on UART1 rx=%d tx=%d baud=%lu\n",
                  MEGA_SERIAL_RX, MEGA_SERIAL_TX, (unsigned long)MEGA_SERIAL_BAUD);

    Serial.println("[BOOT] Ready");
}

// =============================================================================
// loop()
// =============================================================================

void loop() {
    // ── Handle incoming LoRa frames from players ────────────────────────────
    if (rx_flag) {
        rx_flag = false;
        uint8_t buf[CATAN_MAX_FRAME];
        int len = radio.getPacketLength();
        if (len > 0 && len <= (int)sizeof(buf)) {
            int rc = radio.readData(buf, len);
            if (rc == RADIOLIB_ERR_NONE) {
                if (buf[0] == CATAN_WIRE_MAGIC &&
                    len >= CATAN_FRAME_HEADER &&
                    buf[1] + CATAN_FRAME_HEADER <= len) {
                    stats_rx_total++;
                    // Forward PlayerInput directly to Mega over serial
                    uint8_t flen = (uint8_t)(CATAN_FRAME_HEADER + buf[1]);
                    MegaSerial.write(buf, flen);
                    stats_serial_tx++;
                    Serial.printf("[LORA] RX PlayerInput len=%u rssi=%.1f -> Mega\n",
                                  buf[1], radio.getRSSI());
                } else {
                    Serial.printf("[LORA] bad frame len=%d magic=0x%02X\n", len, buf[0]);
                }
            } else {
                Serial.printf("[LORA] readData err=%d\n", rc);
            }
        }
        // Go back to RX mode so we're ready for the next one
        radio.startReceive();
    }

    // ── Process incoming bytes from Mega (BoardState frames) ──────────────
    processSerialRx();

    // ── Re-broadcast latest BoardState over LoRa (rate-limited) ─────────────
    if (state_dirty && (millis() - last_tx_ms >= LORA_BROADCAST_MIN_MS)) {
        uint8_t frame[CATAN_MAX_FRAME];
        uint8_t flen;
        noInterrupts();
        flen = last_state_len;
        memcpy(frame, last_state_frame, flen);
        state_dirty = false;
        interrupts();

        if (flen >= CATAN_FRAME_HEADER) {
            int rc = radio.transmit(frame, flen);
            last_tx_ms = millis();
            if (rc == RADIOLIB_ERR_NONE) {
                stats_tx_total++;
                if ((stats_tx_total % 10) == 0) {
                    Serial.printf("[LORA] TX BoardState len=%u (total=%lu)\n",
                                  flen, (unsigned long)stats_tx_total);
                }
                decodeSummary();
            } else {
                Serial.printf("[LORA] transmit err=%d\n", rc);
            }
            // transmit() blocks and leaves radio idle; return to RX
            radio.startReceive();
        }
    }

    // ── Periodic OLED refresh ───────────────────────────────────────────────
    if (millis() - last_oled_ms >= OLED_REFRESH_MS) {
        last_oled_ms = millis();
        refreshOled();
    }
}
