// =============================================================================
// main.cpp — Catan ESP32-C6 handheld client entry point.
//
// Wires together:
//   ble_client  — BLE central; connects to the Catan hub and exchanges messages
//   input       — Debounced 3-button driver
//   ui          — OLED screens and navigation
//   display     — U8g2 driver wrapper
//
// Flow:
//   setup()  — initialise all modules
//   loop()   — tick input, ble_client, and ui on every iteration
// =============================================================================

#include <Arduino.h>

#include "config.h"
#include "catan_log.h"
#include "display.h"
#include "input.h"
#include "ui.h"
#include "ble_client.h"
#include "catan_wire.h"
#include "proto/catan.pb.h"

// ── BLE → UI callbacks ────────────────────────────────────────────────────────

static void onBoardState(const catan_BoardState& state) {
    ui::onBoardState(state);
}

static void onSlot(uint8_t slot) {
    ui::onSlotAssigned(slot);
}

static void onConnect() {
    ui::onBleStatus(ui::BleStatus::CONNECTED);
    LOGI("MAIN", "Hub connected");
}

static void onDisconnect() {
    ui::onBleStatus(ui::BleStatus::DISCONNECTED);
    LOGI("MAIN", "Hub disconnected");
}

// ── UI → BLE callbacks ────────────────────────────────────────────────────────

static void sendAction(catan_PlayerAction action) {
    catan_PlayerInput input = catan_PlayerInput_init_zero;
    input.proto_version = CATAN_PROTO_VERSION;
    input.action        = action;
    ble_client::sendInput(input);
    LOGI("MAIN", "Action sent: %u", (unsigned)action);
}

static void sendReport(uint32_t vp,
                        uint32_t lumber, uint32_t wool,
                        uint32_t grain,  uint32_t brick,
                        uint32_t ore) {
    catan_PlayerInput input = catan_PlayerInput_init_zero;
    input.proto_version = CATAN_PROTO_VERSION;
    input.action        = catan_PlayerAction_ACTION_REPORT;
    input.vp            = vp;
    input.res_lumber    = lumber;
    input.res_wool      = wool;
    input.res_grain     = grain;
    input.res_brick     = brick;
    input.res_ore       = ore;
    ble_client::sendInput(input);
    LOGI("MAIN", "Report sent vp=%u", (unsigned)vp);
}

// ── Input → UI callback ───────────────────────────────────────────────────────

static void onButton(const input::Event& evt) {
    ui::onButton(evt);
}

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUD);
    // Brief delay so the serial monitor can attach before early log lines.
    delay(200);
    LOGI("MAIN", "Catan client booting …");

    display::init();
    ui::init(sendAction, sendReport);
    ui::onBleStatus(ui::BleStatus::SCANNING);

    input::init(onButton);

    ble_client::init(onBoardState, onSlot, onConnect, onDisconnect);

    LOGI("MAIN", "Setup complete");
}

void loop() {
    input::tick();
    ble_client::tick();
    ui::tick();
}
