// =============================================================================
// main.cpp — Settlers of Catan Player Station (Heltec WiFi LoRa 32 V3)
//
// One player station per seat (x4).  Each:
//   - Receives BoardState frames from the bridge via LoRa and caches them.
//   - Notifies the latest BoardState over BLE to the mobile app.
//   - Relays PlayerInput frames from BLE (or from the 3 local buttons)
//     back to the bridge via LoRa.
//   - Renders a compact summary of the game state on the built-in OLED.
//
// Wire framing (shared on every hop):  [0xCA][len][nanopb payload]
// See `catan_wire.h` for constants.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <NimBLEDevice.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "config.h"
#include "catan_wire.h"
#include "catan_log.h"
#include "proto/catan.pb.h"

static const uint8_t MY_ID = (uint8_t)CATAN_PLAYER_ID;

// =============================================================================
// LoRa radio
// =============================================================================

static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
static volatile bool     rx_flag    = false;
static volatile uint32_t rx_isr_cnt = 0;   // # DIO1 firings (any cause)
IRAM_ATTR static void onDio1() { rx_flag = true; rx_isr_cnt++; }

// Packet-level counters (not in ISR).
static uint32_t rx_packets       = 0;  // readData() succeeded
static uint32_t rx_bad_frames    = 0;  // decode/validation failures
static uint32_t rx_board_states  = 0;  // valid BoardState deliveries

// =============================================================================
// OLED
// =============================================================================

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, /* reset */ OLED_RST, /* scl */ OLED_SCL, /* sda */ OLED_SDA);

// =============================================================================
// Cached BoardState — updated whenever a fresh LoRa frame arrives.
// =============================================================================

static uint8_t state_frame[CATAN_MAX_FRAME];
static uint8_t state_frame_len = 0;
static catan_BoardState state = catan_BoardState_init_zero;
static bool    state_valid = false;
static uint32_t last_state_ms = 0;

// Latest self-reported inventory for THIS player (cached from ACTION_REPORT).
static uint32_t my_vp           = 0;
static uint32_t my_resources[5] = {0, 0, 0, 0, 0};

// =============================================================================
// BLE
// =============================================================================

static NimBLECharacteristic* chr_state = nullptr;
static NimBLECharacteristic* chr_input = nullptr;
static bool                  ble_connected = false;

// =============================================================================
// Outbound reliability — one-slot retry queue for reliable PlayerInputs.
//
// A reliable envelope is retransmitted with exponential backoff until the
// matching Ack arrives (ack_sender == MY_NODE_ID && ack_seq == pending.seq)
// or RELIABLE_MAX_ATTEMPTS is reached. Unreliable envelopes (e.g. periodic
// READY beacons) never populate this slot.
// =============================================================================

static const uint32_t MY_NODE_ID = catan_node_player((uint32_t)MY_ID);

static constexpr uint8_t  RELIABLE_MAX_ATTEMPTS = 4;
static constexpr uint32_t RELIABLE_BASE_BACKOFF_MS = 200;

static uint32_t tx_seq = 0;

struct Pending {
    uint8_t  frame[CATAN_MAX_FRAME];
    uint8_t  frame_len  = 0;
    uint32_t seq        = 0;
    uint32_t last_tx_ms = 0;
    uint8_t  attempts   = 0;   // 0 = slot empty
};
static Pending pending;

static inline uint32_t nextTxSeq() {
    if (++tx_seq == 0) tx_seq = 1;
    return tx_seq;
}

// Low-level framed envelope transmit (no retry tracking).
//
// Sequence follows the heltec_esp32_lora_v3 reference (LoRa_rx_tx.ino):
//   1. clearDio1Action() — detach the user RX ISR so it can't fire on the
//      intermediate SX126x events (SyncWordValid, HeaderValid, TxDone) that
//      RadioLib internally polls while `transmit()` blocks.
//   2. radio.transmit(...) — blocks until TX_DONE, leaves chip in STANDBY.
//   3. setDio1Action(onDio1) + startReceive(INF) — re-attach the user ISR and
//      return to continuous-RX mode. Without the explicit RX_TIMEOUT_INF
//      arg some RadioLib versions default to single-shot RX which silently
//      drops you back to standby after one missed packet.
static int armReceive() {
    rx_flag = false;
    radio.setDio1Action(onDio1);
    return radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF);
}

static bool transmitFrame(const uint8_t* frame, uint8_t len) {
    radio.clearDio1Action();
    int rc = radio.transmit(const_cast<uint8_t*>(frame), len);
    if (rc != RADIOLIB_ERR_NONE) {
        LOGW("LORA", "tx rc=%d len=%u", rc, (unsigned)len);
    }
    int sr_rc = armReceive();
    if (sr_rc != RADIOLIB_ERR_NONE) {
        LOGW("LORA", "post-tx startReceive rc=%d", sr_rc);
    }
    return rc == RADIOLIB_ERR_NONE;
}

// Encode envelope into a frame buffer; returns frame length or 0 on failure.
static size_t encodeEnvelope(catan_Envelope& env, uint8_t reliable,
                             uint8_t* frame, size_t frame_cap) {
    env.proto_version   = CATAN_PROTO_VERSION;
    env.sender_id       = MY_NODE_ID;
    env.sequence_number = nextTxSeq();
    env.timestamp_ms    = millis();
    env.reliable        = (bool)reliable;
    env.message_type    = (catan_MessageType)env.which_body;
    return catan_wire_encode(&env, frame, frame_cap);
}

// Send a PlayerInput envelope. `reliable` inputs take the retry slot.
static void sendPlayerInput(catan_PlayerInput& in, bool reliable) {
    // Always force our player id and strip any caller-supplied transport meta.
    in.player_id = MY_ID;

    catan_Envelope env = catan_Envelope_init_zero;
    env.which_body = catan_Envelope_player_input_tag;
    env.body.player_input = in;

    uint8_t  frame[CATAN_MAX_FRAME];
    const size_t n = encodeEnvelope(env, reliable, frame, sizeof(frame));
    if (n == 0) {
        LOGE("INPUT", "encode fail action=%u", (unsigned)in.action);
        return;
    }

    if (reliable) {
        // Install into pending slot (overwrites any previous pending input —
        // Phase 1 accepts that trade-off: users rarely press buttons faster
        // than the Ack round-trip).
        memcpy(pending.frame, frame, n);
        pending.frame_len  = (uint8_t)n;
        pending.seq        = env.sequence_number;
        pending.last_tx_ms = millis();
        pending.attempts   = 1;
    }

    transmitFrame(frame, (uint8_t)n);
    LOGI("TX", "action=%u seq=%lu reliable=%u",
         (unsigned)in.action, (unsigned long)env.sequence_number,
         (unsigned)reliable);
}

// Retransmit a pending reliable envelope if its backoff has elapsed.
static void servicePending() {
    if (pending.attempts == 0) return;
    if (pending.attempts >= RELIABLE_MAX_ATTEMPTS) {
        LOGW("TX", "seq=%lu gave up after %u attempts",
             (unsigned long)pending.seq, (unsigned)pending.attempts);
        pending.attempts = 0;
        return;
    }
    // Exponential backoff: 200, 400, 800, 1600 ms ...
    uint32_t backoff = RELIABLE_BASE_BACKOFF_MS << (pending.attempts - 1);
    if (millis() - pending.last_tx_ms < backoff) return;

    LOGI("TX", "retx seq=%lu attempt=%u",
         (unsigned long)pending.seq, (unsigned)(pending.attempts + 1));
    pending.last_tx_ms = millis();
    pending.attempts++;
    transmitFrame(pending.frame, pending.frame_len);
}

// Ack receipt — clear pending slot if it matches.
static void handleAck(const catan_Ack& ack) {
    if (pending.attempts == 0) return;
    if (ack.ack_sender != MY_NODE_ID) return;
    if (ack.ack_seq != pending.seq) return;
    LOGI("TX", "ACK seq=%lu (after %u attempts)",
         (unsigned long)pending.seq, (unsigned)pending.attempts);
    pending.attempts = 0;
}

static void sendAction(catan_PlayerAction action) {
    catan_PlayerInput in = catan_PlayerInput_init_zero;
    in.action = action;
    // READY beacons are periodic and tolerant of loss; everything else is
    // user-visible input that MUST reach the board.
    const bool reliable = (action != catan_PlayerAction_ACTION_READY &&
                           action != catan_PlayerAction_ACTION_NONE);
    sendPlayerInput(in, reliable);
}

// =============================================================================
// BLE: input characteristic writes from the mobile app
// =============================================================================

class InputCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        const std::string v = c->getValue();
        const uint8_t*    buf = reinterpret_cast<const uint8_t*>(v.data());
        size_t            len = v.size();
        if (len < CATAN_FRAME_HEADER || buf[0] != CATAN_WIRE_MAGIC) {
            LOGW("BLE", "bad frame len=%u magic=0x%02X",
                 (unsigned)len, len ? buf[0] : 0);
            return;
        }

        catan_Envelope env;
        if (!catan_wire_decode(buf, len, &env)) {
            LOGW("BLE", "envelope decode failed");
            return;
        }
        if (!catan_wire_envelope_valid(&env)) {
            LOGW("BLE", "envelope rejected proto=%u", (unsigned)env.proto_version);
            return;
        }
        if (env.which_body != catan_Envelope_player_input_tag) {
            LOGW("BLE", "unexpected body tag=%u", (unsigned)env.which_body);
            return;
        }

        // Extract the PlayerInput body. We re-wrap with our own envelope
        // before LoRa TX so that (a) player_id cannot be spoofed and (b)
        // the LoRa seq-space belongs to the station, not the mobile.
        catan_PlayerInput in = env.body.player_input;

        if (in.action == catan_PlayerAction_ACTION_REPORT) {
            my_vp            = in.vp;
            my_resources[0]  = in.res_lumber;
            my_resources[1]  = in.res_wool;
            my_resources[2]  = in.res_grain;
            my_resources[3]  = in.res_brick;
            my_resources[4]  = in.res_ore;
        }

        LOGI("BLE", "mobile seq=%lu action=%u vp=%u",
             (unsigned long)env.sequence_number,
             (unsigned)in.action, (unsigned)in.vp);

        // Reports are high-frequency and best-effort; everything else
        // matters and gets the retry slot.
        const bool reliable = (in.action != catan_PlayerAction_ACTION_REPORT &&
                               in.action != catan_PlayerAction_ACTION_NONE &&
                               in.action != catan_PlayerAction_ACTION_READY);
        sendPlayerInput(in, reliable);
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        ble_connected = true;
        LOGI("BLE", "central connected");
    }
    void onDisconnect(NimBLEServer*) override {
        ble_connected = false;
        LOGI("BLE", "central disconnected");
        NimBLEDevice::startAdvertising();
    }
};

static void setupBle() {
    char name[16];
    snprintf(name, sizeof(name), "Catan-P%u", MY_ID + 1);
    NimBLEDevice::init(name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(185);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* svc = server->createService(CATAN_BLE_SERVICE_UUID);
    chr_state = svc->createCharacteristic(
        CATAN_BLE_STATE_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    chr_input = svc->createCharacteristic(
        CATAN_BLE_INPUT_UUID,
        NIMBLE_PROPERTY::WRITE);
    chr_input->setCallbacks(new InputCallbacks());
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(CATAN_BLE_SERVICE_UUID);
    adv->setName(name);
    adv->start();
    LOGI("BLE", "advertising as %s", name);
}

// =============================================================================
// OLED rendering
// =============================================================================

static const char* phaseName(catan_GamePhase p) {
    switch (p) {
        case catan_GamePhase_PHASE_LOBBY:             return "LOBBY";
        case catan_GamePhase_PHASE_BOARD_SETUP:       return "SETUP";
        case catan_GamePhase_PHASE_NUMBER_REVEAL:     return "REVEAL";
        case catan_GamePhase_PHASE_INITIAL_PLACEMENT: return "PLACEMENT";
        case catan_GamePhase_PHASE_PLAYING:           return "PLAYING";
        case catan_GamePhase_PHASE_ROBBER:            return "ROBBER";
        case catan_GamePhase_PHASE_GAME_OVER:         return "OVER";
    }
    return "?";
}

static void renderOled() {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    char line[32];

    snprintf(line, sizeof(line), "P%u  BLE:%s",
             MY_ID + 1, ble_connected ? "Y" : "-");
    oled.drawStr(0, 10, line);

    if (!state_valid) {
        oled.drawStr(0, 28, "Waiting for board...");
        oled.drawStr(0, 62, "LoRa RX listening");
        oled.sendBuffer();
        return;
    }

    snprintf(line, sizeof(line), "%s  cur:P%u/%u",
             phaseName(state.phase),
             (unsigned)state.current_player + 1,
             (unsigned)state.num_players);
    oled.drawStr(0, 22, line);

    if (state.phase == catan_GamePhase_PHASE_NUMBER_REVEAL && state.reveal_number) {
        snprintf(line, sizeof(line), "Reveal: %u", (unsigned)state.reveal_number);
        oled.drawStr(0, 36, line);
    } else if (state.phase == catan_GamePhase_PHASE_INITIAL_PLACEMENT) {
        snprintf(line, sizeof(line), "Rnd%u %s",
                 (unsigned)state.setup_round,
                 state.setup_round <= 1 ? "-> fwd" : "<- rev");
        oled.drawStr(0, 36, line);
    } else if (state.die1 || state.die2) {
        snprintf(line, sizeof(line), "Dice: %u + %u = %u",
                 (unsigned)state.die1, (unsigned)state.die2,
                 (unsigned)(state.die1 + state.die2));
        oled.drawStr(0, 36, line);
    }

    snprintf(line, sizeof(line), "VP %u/%u/%u/%u  me:%u",
             (unsigned)(state.vp_count > 0 ? state.vp[0] : 0),
             (unsigned)(state.vp_count > 1 ? state.vp[1] : 0),
             (unsigned)(state.vp_count > 2 ? state.vp[2] : 0),
             (unsigned)(state.vp_count > 3 ? state.vp[3] : 0),
             (unsigned)my_vp);
    oled.drawStr(0, 50, line);

    if (state.phase == catan_GamePhase_PHASE_GAME_OVER && state.winner_id < 4) {
        snprintf(line, sizeof(line), "WINNER: P%u!", (unsigned)state.winner_id + 1);
        oled.drawStr(0, 62, line);
    } else if (state.current_player == MY_ID) {
        oled.drawStr(0, 62, "** YOUR TURN **");
    } else {
        uint32_t age = (millis() - last_state_ms);
        snprintf(line, sizeof(line), "rx %lums ago", (unsigned long)age);
        oled.drawStr(0, 62, line);
    }

    oled.sendBuffer();
}

// =============================================================================
// Buttons
// =============================================================================

struct Button {
    int      pin;
    bool     last_level;
    uint32_t last_change_ms;
};
static Button buttons[3] = {
    { BTN_LEFT,   true, 0 },
    { BTN_CENTER, true, 0 },
    { BTN_RIGHT,  true, 0 },
};

static catan_PlayerAction actionForButton(uint8_t idx) {
    const catan_GamePhase p = state_valid ? state.phase : catan_GamePhase_PHASE_LOBBY;
    const bool my_turn      = state_valid && state.current_player == MY_ID;

    switch (p) {
        case catan_GamePhase_PHASE_LOBBY:
            return (idx == 0) ? catan_PlayerAction_ACTION_START_GAME
                              : catan_PlayerAction_ACTION_READY;

        case catan_GamePhase_PHASE_BOARD_SETUP:
            // Any button advances to number reveal.
            return catan_PlayerAction_ACTION_NEXT_NUMBER;

        case catan_GamePhase_PHASE_NUMBER_REVEAL:
            return (idx == 1 || idx == 2) ? catan_PlayerAction_ACTION_NEXT_NUMBER
                                          : catan_PlayerAction_ACTION_NONE;

        case catan_GamePhase_PHASE_INITIAL_PLACEMENT:
            if (!my_turn) return catan_PlayerAction_ACTION_NONE;
            return (idx == 1) ? catan_PlayerAction_ACTION_PLACE_DONE
                              : catan_PlayerAction_ACTION_NONE;

        case catan_GamePhase_PHASE_PLAYING:
            if (!my_turn) return catan_PlayerAction_ACTION_NONE;
            if (idx == 0) return catan_PlayerAction_ACTION_ROLL_DICE;
            if (idx == 2) return catan_PlayerAction_ACTION_END_TURN;
            return catan_PlayerAction_ACTION_NONE;

        case catan_GamePhase_PHASE_ROBBER:
            if (!my_turn) return catan_PlayerAction_ACTION_NONE;
            return (idx == 1) ? catan_PlayerAction_ACTION_SKIP_ROBBER
                              : catan_PlayerAction_ACTION_NONE;

        default:
            return catan_PlayerAction_ACTION_NONE;
    }
}

static void pollButtons() {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < 3; ++i) {
        Button& b = buttons[i];
        if (b.pin < 0) continue;
        bool level = digitalRead(b.pin);
        if (level != b.last_level && (now - b.last_change_ms) >= BUTTON_DEBOUNCE_MS) {
            b.last_change_ms = now;
            b.last_level     = level;
            if (!level) {
                catan_PlayerAction a = actionForButton(i);
                LOGI("BTN", "%u pressed -> action=%u", (unsigned)i, (unsigned)a);
                if (a != catan_PlayerAction_ACTION_NONE) sendAction(a);
            }
        }
    }
}

// =============================================================================
// BoardState RX / decode / BLE notify
// =============================================================================

static void handleIncomingFrame(const uint8_t* buf, uint8_t frame_len) {
    if (frame_len < CATAN_FRAME_HEADER || buf[0] != CATAN_WIRE_MAGIC) {
        // First few bytes help spot wrong magic / byte shift / LoRa junk.
        LOGW("LORA", "bad frame len=%u bytes=%02X %02X %02X %02X",
             (unsigned)frame_len,
             frame_len > 0 ? buf[0] : 0,
             frame_len > 1 ? buf[1] : 0,
             frame_len > 2 ? buf[2] : 0,
             frame_len > 3 ? buf[3] : 0);
        return;
    }

    catan_Envelope env;
    if (!catan_wire_decode(buf, frame_len, &env)) {
        LOGW("LORA", "envelope decode failed (len=%u payload_len=%u)",
             (unsigned)frame_len, (unsigned)buf[1]);
        return;
    }
    if (!catan_wire_envelope_valid(&env)) {
        LOGW("LORA", "envelope invalid proto=%u sender=%lu mt=%u body_tag=%u",
             (unsigned)env.proto_version,
             (unsigned long)env.sender_id,
             (unsigned)env.message_type,
             (unsigned)env.which_body);
        return;
    }

    switch (env.which_body) {
        case catan_Envelope_ack_tag:
            handleAck(env.body.ack);
            return;

        case catan_Envelope_nack_tag:
            // A Nack tells us not to retry; treat it like a late Ack.
            if (pending.attempts && env.body.nack.ack_sender == MY_NODE_ID &&
                env.body.nack.ack_seq == pending.seq) {
                LOGW("LORA", "NACK seq=%lu reason=%u",
                     (unsigned long)pending.seq, (unsigned)env.body.nack.reason);
                pending.attempts = 0;
            }
            return;

        case catan_Envelope_board_state_tag: {
            state           = env.body.board_state;
            state_valid     = true;
            last_state_ms   = millis();
            rx_board_states++;
            LOGI("LORA", "rx BoardState phase=%u cur=P%u d=%u+%u rssi=%.1f snr=%.1f",
                 (unsigned)state.phase, (unsigned)(state.current_player + 1),
                 (unsigned)state.die1, (unsigned)state.die2,
                 (double)radio.getRSSI(), (double)radio.getSNR());
            // Forward the original framed envelope straight to BLE so the
            // mobile app sees the same bytes we saw (envelope-level view).
            uint8_t copy_len = (uint8_t)min((int)frame_len, (int)sizeof(state_frame));
            state_frame_len = copy_len;
            memcpy(state_frame, buf, copy_len);
            if (chr_state) {
                chr_state->setValue(state_frame, state_frame_len);
                if (ble_connected) chr_state->notify();
            }
            return;
        }

        default:
            // Anything not listed above (e.g. PlayerInput from another
            // station, SyncRequest, ...) — should not normally appear on
            // this station's RX. Log it so we can see what's on the air.
            if (env.sender_id == MY_NODE_ID) {
                // Hearing ourselves. Either another board was accidentally
                // flashed with the same CATAN_PLAYER_ID, or we have near-
                // field radio feedback. Either way — big red flag.
                LOGE("LORA", "SELF-ECHO body=%u seq=%lu t=%lums rssi=%.1f",
                     (unsigned)env.which_body,
                     (unsigned long)env.sequence_number,
                     (unsigned long)env.timestamp_ms,
                     (double)radio.getRSSI());
            } else if (env.which_body == catan_Envelope_player_input_tag) {
                const catan_PlayerInput& in = env.body.player_input;
                LOGW("LORA", "foreign PlayerInput sender=%lu seq=%lu action=%u pid=%u rssi=%.1f",
                     (unsigned long)env.sender_id,
                     (unsigned long)env.sequence_number,
                     (unsigned)in.action, (unsigned)in.player_id,
                     (double)radio.getRSSI());
            } else {
                LOGW("LORA", "unexpected body tag=%u from sender=%lu seq=%lu",
                     (unsigned)env.which_body,
                     (unsigned long)env.sender_id,
                     (unsigned long)env.sequence_number);
            }
            return;
    }
}

// =============================================================================
// setup() / loop()
// =============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println("====================================");
    Serial.printf (" Catan Player P%u (Heltec V3)\n", MY_ID + 1);
    Serial.printf (" proto v%u\n", CATAN_PROTO_VERSION);
    Serial.println("====================================");

    pinMode(OLED_VEXT, OUTPUT);
    digitalWrite(OLED_VEXT, LOW);
    delay(50);

    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);  delay(20);
    digitalWrite(OLED_RST, HIGH); delay(20);

    Wire.begin(OLED_SDA, OLED_SCL);
    oled.begin();
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 12, "Catan Player");
    char boot_line[24];
    snprintf(boot_line, sizeof(boot_line), "ID: P%u", MY_ID + 1);
    oled.drawStr(0, 28, boot_line);
    oled.drawStr(0, 44, "Booting...");
    oled.sendBuffer();

    for (uint8_t i = 0; i < 3; ++i) {
        if (buttons[i].pin >= 0) {
            pinMode(buttons[i].pin, INPUT_PULLUP);
            // Read actual state so the first poll never fires a spurious action.
            buttons[i].last_level     = digitalRead(buttons[i].pin);
            buttons[i].last_change_ms = millis();
        }
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    int rc = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                         LORA_SYNCWORD, LORA_POWER_DBM, LORA_PREAMBLE_LEN,
                         LORA_TCXO_V, LORA_USE_LDO);
    if (rc != RADIOLIB_ERR_NONE) {
        LOGE("LORA", "begin FAIL rc=%d", rc);
        oled.drawStr(0, 60, "LoRa FAIL");
        oled.sendBuffer();
        while (true) delay(1000);
    }
    radio.setDio2AsRfSwitch(true);
    int sr_rc = armReceive();
    LOGI("LORA", "listening (%.1fMHz BW=%.0fkHz SF%u tcxo=%.1fV node_id=%lu startRx=%d)",
         (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, (unsigned)LORA_SF,
         (double)LORA_TCXO_V, (unsigned long)MY_NODE_ID, sr_rc);

    setupBle();

    LOGI("BOOT", "ready, sending initial READY beacon");
    sendAction(catan_PlayerAction_ACTION_READY);
}

static uint32_t last_ready_ms = 0;
static uint32_t last_oled_ms  = 0;
static uint32_t last_hb_ms    = 0;
static constexpr uint32_t HEARTBEAT_MS = 5000;

void loop() {
    if (rx_flag) {
        rx_flag = false;
        uint8_t buf[CATAN_MAX_FRAME];
        int len = radio.getPacketLength();
        if (len > 0 && len <= (int)sizeof(buf)) {
            int rc = radio.readData(buf, len);
            if (rc == RADIOLIB_ERR_NONE) {
                rx_packets++;
                handleIncomingFrame(buf, (uint8_t)len);
            } else {
                rx_bad_frames++;
                LOGW("LORA", "readData rc=%d (rssi=%.1f)",
                     rc, (double)radio.getRSSI());
            }
        } else if (len > 0) {
            rx_bad_frames++;
            LOGW("LORA", "oversized pkt len=%d", len);
        }
        armReceive();
    }

    pollButtons();
    servicePending();

    // Keep announcing readiness while still in lobby so the board picks
    // us up even if the first READY was dropped.
    if ((!state_valid || state.phase == catan_GamePhase_PHASE_LOBBY) &&
        (millis() - last_ready_ms >= READY_REPEAT_MS)) {
        last_ready_ms = millis();
        sendAction(catan_PlayerAction_ACTION_READY);
    }

    if (millis() - last_oled_ms >= OLED_REFRESH_MS) {
        last_oled_ms = millis();
        renderOled();
    }

    if (millis() - last_hb_ms >= HEARTBEAT_MS) {
        last_hb_ms = millis();
        uint32_t state_age = last_state_ms ? (millis() - last_state_ms) : 0;
        LOGI("HB", "ble=%s isr=%lu rx_pkts=%lu rx_bs=%lu rx_bad=%lu state_valid=%u age=%lums pending=%u vp=%lu",
             ble_connected ? "Y" : "-",
             (unsigned long)rx_isr_cnt,
             (unsigned long)rx_packets,
             (unsigned long)rx_board_states,
             (unsigned long)rx_bad_frames,
             (unsigned)state_valid,
             (unsigned long)state_age,
             (unsigned)pending.attempts,
             (unsigned long)my_vp);
    }
}
