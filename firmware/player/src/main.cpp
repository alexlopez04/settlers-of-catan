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
#include "proto/catan.pb.h"

static const uint8_t MY_ID = (uint8_t)CATAN_PLAYER_ID;

// =============================================================================
// LoRa radio
// =============================================================================

static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
static volatile bool rx_flag = false;
IRAM_ATTR static void onDio1() { rx_flag = true; }

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
// Send PlayerInput over LoRa (helper)
// =============================================================================

static void sendInput(const catan_PlayerInput& in) {
    uint8_t frame[CATAN_MAX_FRAME];
    pb_ostream_t os = pb_ostream_from_buffer(frame + CATAN_FRAME_HEADER,
                                             sizeof(frame) - CATAN_FRAME_HEADER);
    if (!pb_encode(&os, catan_PlayerInput_fields, &in)) {
        Serial.printf("[INPUT] encode fail: %s\n", PB_GET_ERROR(&os));
        return;
    }
    frame[0] = CATAN_WIRE_MAGIC;
    frame[1] = (uint8_t)os.bytes_written;
    int rc = radio.transmit(frame, CATAN_FRAME_HEADER + os.bytes_written);
    if (rc == RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] TX PlayerInput action=%u len=%u\n",
                      (unsigned)in.action, (unsigned)os.bytes_written);
    } else {
        Serial.printf("[LORA] TX err=%d\n", rc);
    }
    radio.startReceive();
}

static void sendAction(catan_PlayerAction action) {
    catan_PlayerInput in = catan_PlayerInput_init_zero;
    in.proto_version = CATAN_PROTO_VERSION;
    in.player_id     = MY_ID;
    in.action        = action;
    sendInput(in);
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
            Serial.printf("[BLE] bad frame len=%u magic=0x%02X\n",
                          (unsigned)len, len ? buf[0] : 0);
            return;
        }
        uint8_t pl = buf[1];
        if (pl == 0 || pl + CATAN_FRAME_HEADER > len) return;

        catan_PlayerInput in = catan_PlayerInput_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf + CATAN_FRAME_HEADER, pl);
        if (!pb_decode(&is, catan_PlayerInput_fields, &in)) {
            Serial.printf("[BLE] decode fail: %s\n", PB_GET_ERROR(&is));
            return;
        }

        // Force our player id — don't trust the phone.
        in.player_id     = MY_ID;
        in.proto_version = CATAN_PROTO_VERSION;

        if (in.action == catan_PlayerAction_ACTION_REPORT) {
            my_vp            = in.vp;
            my_resources[0]  = in.res_lumber;
            my_resources[1]  = in.res_wool;
            my_resources[2]  = in.res_grain;
            my_resources[3]  = in.res_brick;
            my_resources[4]  = in.res_ore;
        }

        Serial.printf("[BLE] action=%u vp=%u\n",
                      (unsigned)in.action, (unsigned)in.vp);
        sendInput(in);
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        ble_connected = true;
        Serial.println("[BLE] central connected");
    }
    void onDisconnect(NimBLEServer*) override {
        ble_connected = false;
        Serial.println("[BLE] central disconnected");
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
    Serial.printf("[BLE] advertising as %s\n", name);
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
                Serial.printf("[BTN] %u pressed -> action=%u\n", i, (unsigned)a);
                if (a != catan_PlayerAction_ACTION_NONE) sendAction(a);
            }
        }
    }
}

// =============================================================================
// BoardState RX / decode / BLE notify
// =============================================================================

static void handleIncomingFrame(const uint8_t* buf, uint8_t frame_len) {
    if (frame_len < CATAN_FRAME_HEADER || buf[0] != CATAN_WIRE_MAGIC) return;
    uint8_t pl = buf[1];
    if (pl == 0 || pl + CATAN_FRAME_HEADER > frame_len) return;

    catan_BoardState incoming = catan_BoardState_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf + CATAN_FRAME_HEADER, pl);
    if (!pb_decode(&is, catan_BoardState_fields, &incoming)) {
        Serial.printf("[LORA] decode fail: %s\n", PB_GET_ERROR(&is));
        return;
    }
    if (incoming.proto_version != CATAN_PROTO_VERSION) {
        Serial.printf("[LORA] proto mismatch: %u != %u\n",
                      (unsigned)incoming.proto_version,
                      (unsigned)CATAN_PROTO_VERSION);
        return;
    }

    state           = incoming;
    state_valid     = true;
    last_state_ms   = millis();
    state_frame_len = (uint8_t)(CATAN_FRAME_HEADER + pl);
    memcpy(state_frame, buf, state_frame_len);

    if (chr_state) {
        chr_state->setValue(state_frame, state_frame_len);
        if (ble_connected) chr_state->notify();
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
                         LORA_SYNCWORD, LORA_POWER_DBM, LORA_PREAMBLE_LEN);
    if (rc != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] begin FAIL: %d\n", rc);
        oled.drawStr(0, 60, "LoRa FAIL");
        oled.sendBuffer();
        while (true) delay(1000);
    }
    radio.setDio1Action(onDio1);
    radio.startReceive();
    Serial.println("[LORA] listening");

    setupBle();

    Serial.println("[BOOT] ready");
    sendAction(catan_PlayerAction_ACTION_READY);
}

static uint32_t last_ready_ms = 0;
static uint32_t last_oled_ms  = 0;

void loop() {
    if (rx_flag) {
        rx_flag = false;
        uint8_t buf[CATAN_MAX_FRAME];
        int len = radio.getPacketLength();
        if (len > 0 && len <= (int)sizeof(buf)) {
            int rc = radio.readData(buf, len);
            if (rc == RADIOLIB_ERR_NONE) {
                handleIncomingFrame(buf, (uint8_t)len);
            } else {
                Serial.printf("[LORA] readData err=%d\n", rc);
            }
        }
        radio.startReceive();
    }

    pollButtons();

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
}
