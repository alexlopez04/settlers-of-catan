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
#include "catan_log.h"
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
// LoRa sequencing follows the heltec_esp32_lora_v3 reference exactly:
//
//   Before `transmit()` — clearDio1Action() detaches our RX ISR so it
//     cannot fire on intermediate SX126x events while RadioLib polls for
//     TX_DONE internally. (Leaving the user ISR attached during transmit()
//     causes rx_flag to latch on e.g. SyncWordValid of concurrent traffic
//     and corrupts the post-TX state machine.)
//
//   After `transmit()`  — setDio1Action(onDio1) + startReceive(INF) puts
//     us back in continuous-RX mode with a working user ISR. The explicit
//     RADIOLIB_SX126X_RX_TIMEOUT_INF argument guards against RadioLib
//     version drift toward single-shot RX defaults.
//
// Never call `radio.startReceive()` directly — always go through
// `armReceive()` so ISR + flag state stay in lockstep.
static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
static volatile bool rx_flag = false;
IRAM_ATTR static void onDio1() { rx_flag = true; }

static int armReceive() {
    rx_flag = false;
    radio.setDio1Action(onDio1);
    return radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF);
}

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
static uint32_t stats_rx_total      = 0;   // LoRa frames received (any kind)
static uint32_t stats_tx_total      = 0;   // LoRa frames transmitted (BoardState)
static uint32_t stats_serial_rx     = 0;   // Complete BoardState frames from Mega
static uint32_t stats_serial_tx     = 0;   // PlayerInput frames forwarded to Mega
static uint32_t stats_serial_bytes  = 0;   // Raw bytes read from MegaSerial
static uint32_t stats_serial_resync = 0;   // Bad length bytes / framing restarts
static uint32_t stats_lora_errors   = 0;   // transmit/readData errors
static uint32_t last_oled_ms        = 0;
static uint32_t last_tx_ms          = 0;
static uint32_t last_serial_byte_ms = 0;   // when we last saw ANY byte from Mega
static uint32_t last_hb_ms          = 0;
static constexpr uint32_t HEARTBEAT_MS = 5000;

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
        stats_serial_bytes++;
        last_serial_byte_ms = millis();
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
                    LOGW("SERIAL", "bad len=%u from Mega, resync", (unsigned)b);
                    stats_serial_resync++;
                    serial_rx_state = RxState::HUNT;
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

                    // Only frames that are actually BoardStates deserve a
                    // LoRa rebroadcast. Anything else arriving on the Mega
                    // link (Ack, Nack, or — if something is looping back —
                    // an echoed PlayerInput) gets logged and dropped.
                    catan_Envelope env;
                    if (!catan_wire_decode(serial_rx_buf, serial_rx_pos, &env)) {
                        LOGW("SERIAL", "decode failed len=%u",
                             (unsigned)serial_rx_buf[1]);
                        stats_serial_resync++;
                        break;
                    }
                    if (env.which_body != catan_Envelope_board_state_tag) {
                        LOGW("SERIAL", "drop non-BoardState from Mega link: body=%u sender=%lu seq=%lu",
                             (unsigned)env.which_body,
                             (unsigned long)env.sender_id,
                             (unsigned long)env.sequence_number);
                        break;
                    }

                    memcpy(last_state_frame, serial_rx_buf, serial_rx_expect);
                    last_state_len = serial_rx_expect;
                    state_dirty    = true;
                    stats_serial_rx++;
                    LOGD("SERIAL", "RX BoardState len=%u (total=%lu)",
                         (unsigned)serial_rx_buf[1], (unsigned long)stats_serial_rx);
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
    // Decode the cached Envelope just to populate OLED summary fields. The
    // bridge is stateless — it never mutates the frame, only peeks for UI.
    if (last_state_len < CATAN_FRAME_HEADER) return;

    catan_Envelope env;
    if (!catan_wire_decode(last_state_frame, last_state_len, &env)) return;
    if (!catan_wire_envelope_valid(&env)) return;
    if (env.which_body != catan_Envelope_board_state_tag) return;

    const catan_BoardState& s = env.body.board_state;
    summary.valid      = true;
    summary.phase      = (uint8_t)s.phase;
    summary.cur        = (uint8_t)s.current_player;
    summary.np         = (uint8_t)s.num_players;
    summary.d1         = (uint8_t)s.die1;
    summary.d2         = (uint8_t)s.die2;
    summary.updated_ms = millis();
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
    } else if (stats_serial_bytes == 0) {
        // Nothing arriving from the Mega at all — most likely a wiring /
        // level-shift issue. Make it obvious.
        oled.drawStr(0, 24, "No Mega bytes rx!");
        oled.drawStr(0, 36, "Check wiring/levels");
    } else {
        // Bytes arrive but no complete BoardState yet — surface partial progress.
        snprintf(line, sizeof(line), "Mega bytes:%lu",
                 (unsigned long)stats_serial_bytes);
        oled.drawStr(0, 24, line);
        uint32_t age = millis() - last_serial_byte_ms;
        snprintf(line, sizeof(line), "last %lums ago", (unsigned long)age);
        oled.drawStr(0, 36, line);
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
                         LORA_PREAMBLE_LEN,
                         LORA_TCXO_V,
                         LORA_USE_LDO);
    if (rc != RADIOLIB_ERR_NONE) {
        LOGE("LORA", "begin FAILED rc=%d", rc);
        oled.drawStr(0, 48, "LoRa FAIL!");
        oled.sendBuffer();
        while (true) delay(1000);
    }
    // Force DIO2 as RF switch (Heltec V3 has no external switch pin). Already
    // enabled by begin() but calling it explicitly guards against RadioLib
    // version drift and makes the intent visible.
    radio.setDio2AsRfSwitch(true);
    LOGI("LORA", "begin OK (%.1f MHz BW=%.0fkHz SF%u CR4/%u pwr=%ddBm tcxo=%.1fV)",
         (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ,
         (unsigned)LORA_SF, (unsigned)LORA_CR,
         (int)LORA_POWER_DBM, (double)LORA_TCXO_V);
    int sr_rc = armReceive();
    if (sr_rc != RADIOLIB_ERR_NONE) {
        LOGW("LORA", "initial startReceive rc=%d", sr_rc);
    }


    // ── UART link to Mega ─────────────────────────────────────────────
    // Grow RX buffer well past the ESP32 default (256 B) so that a blocking
    // LoRa `radio.transmit()` window (~15–20 ms at SF7/BW500) cannot drop
    // bytes that the Mega is streaming in the meantime.
    MegaSerial.setRxBufferSize(1024);
    MegaSerial.begin(MEGA_SERIAL_BAUD, SERIAL_8N1, MEGA_SERIAL_RX, MEGA_SERIAL_TX);
    LOGI("SERIAL", "Mega UART1 rx=%d tx=%d baud=%lu",
         MEGA_SERIAL_RX, MEGA_SERIAL_TX, (unsigned long)MEGA_SERIAL_BAUD);

    LOGI("BOOT", "ready");
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
                    LOGI("LORA", "rx PlayerInput len=%u rssi=%.1fdBm snr=%.1f -> Mega",
                         (unsigned)buf[1], (double)radio.getRSSI(), (double)radio.getSNR());
                } else {
                    stats_lora_errors++;
                    LOGW("LORA", "bad frame len=%d magic=0x%02X", len, buf[0]);
                }
            } else {
                stats_lora_errors++;
                LOGW("LORA", "readData rc=%d", rc);
            }
        }
        armReceive();
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
            // Detach RX ISR so it can't spuriously latch during the blocking
            // transmit window (see RadioLib/heltec_esp32_lora_v3 reference).
            radio.clearDio1Action();
            uint32_t tx_start = millis();
            int rc = radio.transmit(frame, flen);
            uint32_t tx_ms = millis() - tx_start;
            last_tx_ms = millis();
            if (rc == RADIOLIB_ERR_NONE) {
                stats_tx_total++;
                // Log every TX for the first 20 frames so boot-up connectivity
                // is obvious in the serial monitor, then fall back to every
                // 10th so the log doesn't spam during steady-state.
                if (stats_tx_total <= 20 || (stats_tx_total % 10) == 1) {
                    LOGI("LORA", "tx BoardState len=%u (total=%lu, %lums)",
                         (unsigned)flen, (unsigned long)stats_tx_total,
                         (unsigned long)tx_ms);
                }
                decodeSummary();
            } else {
                stats_lora_errors++;
                LOGW("LORA", "transmit rc=%d (flen=%u total_err=%lu)",
                     rc, (unsigned)flen, (unsigned long)stats_lora_errors);
            }
            int sr_rc = armReceive();
            if (sr_rc != RADIOLIB_ERR_NONE) {
                stats_lora_errors++;
                LOGW("LORA", "post-tx startReceive rc=%d", sr_rc);
            }
        }
    }

    // ── Periodic OLED refresh ───────────────────────────────────────────────
    if (millis() - last_oled_ms >= OLED_REFRESH_MS) {
        last_oled_ms = millis();
        refreshOled();
    }

    // ── Periodic heartbeat log ──────────────────────────────────────────────
    if (millis() - last_hb_ms >= HEARTBEAT_MS) {
        last_hb_ms = millis();
        uint32_t age = last_serial_byte_ms ? (millis() - last_serial_byte_ms) : 0;
        LOGI("HB", "mega_bytes=%lu mega_frames=%lu resync=%lu last_byte=%lums ago | lora_tx=%lu lora_rx=%lu lora_err=%lu",
             (unsigned long)stats_serial_bytes,
             (unsigned long)stats_serial_rx,
             (unsigned long)stats_serial_resync,
             (unsigned long)age,
             (unsigned long)stats_tx_total,
             (unsigned long)stats_rx_total,
             (unsigned long)stats_lora_errors);
    }
}
