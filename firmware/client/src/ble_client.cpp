// =============================================================================
// ble_client.cpp — BLE central implementation (NimBLE-Arduino).
// =============================================================================

#include "ble_client.h"
#include "config.h"
#include "catan_log.h"
#include "catan_wire.h"

#include <NimBLEDevice.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace ble_client {

// ── Internal state ─────────────────────────────────────────────────────────

namespace {

// ── Callbacks from the module user ──────────────────────────────────────────
StateHandler      g_on_state      = nullptr;
SlotHandler       g_on_slot       = nullptr;
ConnectHandler    g_on_connect    = nullptr;
DisconnectHandler g_on_disconnect = nullptr;

// ── BLE handles ──────────────────────────────────────────────────────────────
NimBLEClient*          g_client      = nullptr;
NimBLERemoteCharacteristic* g_char_state    = nullptr;
NimBLERemoteCharacteristic* g_char_input    = nullptr;
NimBLERemoteCharacteristic* g_char_identity = nullptr;
NimBLERemoteCharacteristic* g_char_slot     = nullptr;

// ── Connection state machine ─────────────────────────────────────────────────
enum class State : uint8_t {
    IDLE,         // not yet started
    SCANNING,     // BLE scan running
    CONNECTING,   // connect() called, waiting for result
    SUBSCRIBING,  // discovering service / subscribing
    IDENTIFYING,  // writing Identity char
    CONNECTED,    // fully operational
    DISCONNECTED, // lost connection, waiting to rescan
};

State    g_state        = State::IDLE;
uint32_t g_state_ts     = 0;   // millis() when g_state was last changed
NimBLEAddress g_target;        // address of the hub we found

// Stable client identity built from our MAC address.
char g_client_id[40] = {};

// ── Helpers ──────────────────────────────────────────────────────────────────

static void changeState(State next) {
    g_state    = next;
    g_state_ts = millis();
}

static void buildClientId() {
    const std::string mac = NimBLEDevice::getAddress().toString();
    snprintf(g_client_id, sizeof(g_client_id), "ESP32-%s", mac.c_str());
    LOGI("BLE", "Client ID: %s", g_client_id);
}

// ── NimBLE scan callback ──────────────────────────────────────────────────────

class ScanCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (g_state != State::SCANNING) return;
        if (!dev->isAdvertisingService(NimBLEUUID(CATAN_BLE_SERVICE_UUID))) return;

        LOGI("BLE", "Found hub: %s (%s)",
             dev->getName().c_str(),
             dev->getAddress().toString().c_str());

        NimBLEDevice::getScan()->stop();
        g_target = dev->getAddress();
        changeState(State::CONNECTING);
    }
};

// ── NimBLE client callbacks ───────────────────────────────────────────────────

class ClientCallbacks : public NimBLEClientCallbacks {
public:
    void onConnect(NimBLEClient* /*client*/) override {
        LOGI("BLE", "TCP connected");
        // Transition is handled in tick() after service discovery.
    }

    void onDisconnect(NimBLEClient* /*client*/, int reason) override {
        LOGW("BLE", "Disconnected reason=%d", reason);
        g_char_state    = nullptr;
        g_char_input    = nullptr;
        g_char_identity = nullptr;
        g_char_slot     = nullptr;
        changeState(State::DISCONNECTED);
        if (g_on_disconnect) g_on_disconnect();
    }
};

// ── NimBLE notification callbacks ────────────────────────────────────────────

static void onStateNotify(NimBLERemoteCharacteristic* /*ch*/,
                          uint8_t* data, size_t len, bool is_notify) {
    if (!is_notify) return;
    catan_BoardState bs = catan_BoardState_init_zero;
    if (!catan_decode_board_state(data, len, &bs)) {
        LOGW("BLE", "BoardState decode fail (len=%u)", (unsigned)len);
        return;
    }
    LOGD("BLE", "BoardState phase=%u cur=%u", (unsigned)bs.phase, (unsigned)bs.current_player);
    if (g_on_state) g_on_state(bs);
}

static void onSlotNotify(NimBLERemoteCharacteristic* /*ch*/,
                         uint8_t* data, size_t len, bool is_notify) {
    if (!is_notify || len < 1) return;
    const uint8_t slot = data[0];
    LOGI("BLE", "Slot assigned: %u", (unsigned)slot);
    if (g_on_slot) g_on_slot(slot);
}

// Static instances (no dynamic allocation needed)
static ScanCallbacks   g_scan_cbs;
static ClientCallbacks g_client_cbs;

// ── Service discovery & subscription ─────────────────────────────────────────

static bool discoverAndSubscribe() {
    NimBLERemoteService* svc =
        g_client->getService(NimBLEUUID(CATAN_BLE_SERVICE_UUID));
    if (!svc) {
        LOGE("BLE", "Service not found");
        return false;
    }

    g_char_state = svc->getCharacteristic(NimBLEUUID(CATAN_BLE_STATE_UUID));
    g_char_input = svc->getCharacteristic(NimBLEUUID(CATAN_BLE_INPUT_UUID));
    g_char_identity = svc->getCharacteristic(NimBLEUUID(CATAN_BLE_IDENTITY_UUID));
    g_char_slot  = svc->getCharacteristic(NimBLEUUID(CATAN_BLE_SLOT_UUID));

    if (!g_char_state || !g_char_input || !g_char_identity || !g_char_slot) {
        LOGE("BLE", "Missing characteristic(s)");
        return false;
    }

    if (!g_char_state->subscribe(true, onStateNotify)) {
        LOGE("BLE", "State subscribe fail");
        return false;
    }
    if (!g_char_slot->subscribe(true, onSlotNotify)) {
        LOGE("BLE", "Slot subscribe fail");
        return false;
    }

    return true;
}

}  // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

void init(StateHandler     on_state,
          SlotHandler      on_slot,
          ConnectHandler   on_connect,
          DisconnectHandler on_disconnect) {
    g_on_state      = on_state;
    g_on_slot       = on_slot;
    g_on_connect    = on_connect;
    g_on_disconnect = on_disconnect;

    NimBLEDevice::init("");
    NimBLEDevice::setPower(9);  // +9 dBm — maximum for C6

    buildClientId();

    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&g_client_cbs, false);
    g_client->setConnectionParams(12, 12, 0, 200);  // fast interval
    g_client->setConnectTimeout(5);

    // Configure scanner: passive, 100 ms window, scan forever until stopped
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scan_cbs, false);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(80);

    changeState(State::SCANNING);
    NimBLEDevice::getScan()->start(0, false);  // 0 = indefinite
    LOGI("BLE", "Scanning for %s …", CATAN_BLE_HUB_NAME);
}

void tick() {
    switch (g_state) {

        case State::CONNECTING: {
            // connect() is blocking in NimBLE; call it once and check result.
            LOGI("BLE", "Connecting to %s …", g_target.toString().c_str());
            if (!g_client->connect(g_target)) {
                LOGW("BLE", "Connect failed, retrying scan");
                changeState(State::DISCONNECTED);
                if (g_on_disconnect) g_on_disconnect();
                break;
            }
            // Request large MTU so a full BoardState fits in one ATT packet.
            NimBLEDevice::setMTU(247);
            changeState(State::SUBSCRIBING);
            break;
        }

        case State::SUBSCRIBING: {
            if (!discoverAndSubscribe()) {
                LOGE("BLE", "Discovery failed");
                g_client->disconnect();
                changeState(State::DISCONNECTED);
                if (g_on_disconnect) g_on_disconnect();
                break;
            }
            changeState(State::IDENTIFYING);
            break;
        }

        case State::IDENTIFYING: {
            // The hub requires Identity within SLOT_CLAIM_TIMEOUT_MS.
            // Write it immediately.
            const NimBLEAttValue id_val(
                reinterpret_cast<const uint8_t*>(g_client_id),
                strlen(g_client_id));
            if (!g_char_identity->writeValue(id_val, true)) {
                LOGW("BLE", "Identity write failed");
                g_client->disconnect();
                changeState(State::DISCONNECTED);
                if (g_on_disconnect) g_on_disconnect();
                break;
            }
            LOGI("BLE", "Identity sent: %s", g_client_id);
            changeState(State::CONNECTED);
            if (g_on_connect) g_on_connect();
            break;
        }

        case State::DISCONNECTED: {
            // Wait before rescanning to avoid hammering the radio.
            if ((millis() - g_state_ts) >= RESCAN_DELAY_MS) {
                LOGI("BLE", "Restarting scan …");
                changeState(State::SCANNING);
                NimBLEDevice::getScan()->start(0, false);
            }
            break;
        }

        case State::CONNECTED:
        case State::SCANNING:
        case State::IDLE:
        default:
            break;
    }
}

void sendInput(const catan_PlayerInput& input) {
    if (g_state != State::CONNECTED || !g_char_input) return;

    uint8_t buf[CATAN_MAX_PAYLOAD];
    const size_t n = catan_encode_player_input(&input, buf, sizeof(buf));
    if (n == 0) {
        LOGE("BLE", "PlayerInput encode fail");
        return;
    }

    const NimBLEAttValue val(buf, n);
    if (!g_char_input->writeValue(val, false)) {
        LOGW("BLE", "PlayerInput write fail");
    } else {
        LOGD("BLE", "Sent action=%u", (unsigned)input.action);
    }
}

bool isConnected() {
    return g_state == State::CONNECTED;
}

}  // namespace ble_client
