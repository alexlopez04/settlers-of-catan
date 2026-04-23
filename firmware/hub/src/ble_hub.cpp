// =============================================================================
// ble_hub.cpp — see header. NimBLE multi-connection peripheral with
// per-connection seat assignment by client_id.
// =============================================================================

#include "ble_hub.h"
#include "config.h"
#include "catan_log.h"
#include "player_slots.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

namespace {

NimBLEServer*           g_server         = nullptr;
NimBLECharacteristic*   g_chr_state      = nullptr;
NimBLECharacteristic*   g_chr_input      = nullptr;
NimBLECharacteristic*   g_chr_identity   = nullptr;
NimBLECharacteristic*   g_chr_slot       = nullptr;

ble_hub::InputHandler    g_on_input    = nullptr;
ble_hub::PresenceHandler g_on_presence = nullptr;

// Track when each connection arrived so we can drop centrals that never
// write Identity. NimBLE conn handles are uint16; we keep a small ring.
struct PendingClaim {
    uint16_t conn_handle;
    uint32_t connected_at_ms;
    bool     active;
};
PendingClaim g_pending[MAX_BLE_CONNECTIONS] = {};

void trackPending(uint16_t conn) {
    for (auto& p : g_pending) {
        if (!p.active) {
            p.active          = true;
            p.conn_handle     = conn;
            p.connected_at_ms = millis();
            return;
        }
    }
    LOGW("BLE", "no slot in pending table for conn=%u", (unsigned)conn);
}

void clearPending(uint16_t conn) {
    for (auto& p : g_pending) {
        if (p.active && p.conn_handle == conn) { p.active = false; return; }
    }
}

void notifySlot(uint16_t conn, uint8_t slot) {
    if (!g_chr_slot) return;
    g_chr_slot->setValue(&slot, 1);
    // Notify only this conn so each central learns its own seat.
    g_chr_slot->notify(conn);
}

void writeAdvertisingPayload() {
    // Primary advertisement packet: flags (auto) + 128-bit service UUID = ~21 bytes.
    // The name CANNOT also fit here — 18 (UUID) + 13 (name) + 3 (flags) = 34 > 31 bytes.
    // iOS filters by service UUID from the primary packet, so it must go here.
    NimBLEAdvertisementData advData;
    advData.setCompleteServices(NimBLEUUID(CATAN_BLE_SERVICE_UUID));

    // Scan response packet: name only = 13 bytes, well within the 31-byte budget.
    // CoreBluetooth (iOS) and Android both merge scan response into the device record.
    NimBLEAdvertisementData scanRsp;
    scanRsp.setName(CATAN_BLE_DEVICE_NAME);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanRsp);
}

void startAdvertisingIfRoom() {
    if (!g_server) return;
    if (g_server->getConnectedCount() >= MAX_BLE_CONNECTIONS) {
        NimBLEDevice::stopAdvertising();
        return;
    }
    NimBLEDevice::startAdvertising();
}

// ── Characteristic callbacks ───────────────────────────────────────────────

class IdentityCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        std::string v = c->getValue();
        if (v.empty() || v.size() >= slots::CLIENT_ID_MAX) {
            LOGW("BLE", "Identity bad len=%u", (unsigned)v.size());
            return;
        }
        // Restrict to printable ASCII to keep logs sane.
        for (char ch : v) {
            if (ch < 0x20 || ch > 0x7E) {
                LOGW("BLE", "Identity non-printable byte");
                return;
            }
        }
        char client_id[slots::CLIENT_ID_MAX];
        memcpy(client_id, v.data(), v.size());
        client_id[v.size()] = '\0';

        uint16_t conn = info.getConnHandle();
        uint8_t  slot = slots::claim(client_id, conn);

        notifySlot(conn, slot);

        if (slot == slots::NO_SLOT) {
            LOGW("BLE", "no slot for client '%s' — disconnecting conn=%u",
                 client_id, (unsigned)conn);
            g_server->disconnect(conn);
            return;
        }
        clearPending(conn);
        if (g_on_presence) g_on_presence();
    }
};

class InputCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        uint16_t conn = info.getConnHandle();
        uint8_t  slot = slots::slotForConn(conn);
        if (slot == slots::NO_SLOT) {
            LOGW("BLE", "Input from un-seated conn=%u — ignored",
                 (unsigned)conn);
            return;
        }
        std::string v = c->getValue();
        if (v.empty() || v.size() > CATAN_MAX_PAYLOAD) {
            LOGW("BLE", "Input bad len=%u from slot %u",
                 (unsigned)v.size(), (unsigned)slot);
            return;
        }
        catan_PlayerInput in = catan_PlayerInput_init_zero;
        if (!catan_decode_player_input(
                reinterpret_cast<const uint8_t*>(v.data()), v.size(), &in)) {
            LOGW("BLE", "Input decode fail from slot %u (len=%u)",
                 (unsigned)slot, (unsigned)v.size());
            return;
        }
        // Authoritative re-stamp of identity fields.
        in.proto_version = CATAN_PROTO_VERSION;
        in.player_id     = slot;
        const auto* tbl  = slots::table();
        strncpy(in.client_id, tbl[slot].client_id, sizeof(in.client_id) - 1);
        in.client_id[sizeof(in.client_id) - 1] = '\0';

        if (g_on_input) g_on_input(in);
    }
};

class StateCb : public NimBLECharacteristicCallbacks {
    // Read returns whatever was last set via broadcastBoardState(). NimBLE
    // handles that natively; nothing to override here besides logging.
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info,
                     uint16_t sub_value) override {
        LOGI("BLE", "State subscribed by conn=%u value=0x%04X",
             (unsigned)info.getConnHandle(), (unsigned)sub_value);
    }
};

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* /*srv*/, NimBLEConnInfo& info) override {
        uint16_t conn = info.getConnHandle();
        LOGI("BLE", "connect conn=%u (n_conn=%u)",
             (unsigned)conn, (unsigned)g_server->getConnectedCount());
        trackPending(conn);
        // Push the "no slot yet" sentinel so the app sees something on
        // its initial Slot read.
        notifySlot(conn, slots::NO_SLOT);
        startAdvertisingIfRoom();
    }
    void onDisconnect(NimBLEServer* /*srv*/, NimBLEConnInfo& info,
                      int reason) override {
        uint16_t conn = info.getConnHandle();
        clearPending(conn);
        uint8_t freed = slots::release(conn);
        LOGI("BLE", "disconnect conn=%u reason=%d freed_slot=%u",
             (unsigned)conn, reason, (unsigned)freed);
        if (freed != slots::NO_SLOT && g_on_presence) g_on_presence();
        startAdvertisingIfRoom();
    }
};

}  // namespace

namespace ble_hub {

void init(InputHandler on_input, PresenceHandler on_presence) {
    g_on_input    = on_input;
    g_on_presence = on_presence;

    NimBLEDevice::init(CATAN_BLE_DEVICE_NAME);
    NimBLEDevice::setPower(9);
    NimBLEDevice::setMTU(BLE_MTU);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(new ServerCb());
    // Key for multi-connection support: don't auto-restart advertising on
    // disconnect — we manage it explicitly so we never advertise once we
    // are already at capacity.
    g_server->advertiseOnDisconnect(false);

    NimBLEService* svc = g_server->createService(CATAN_BLE_SERVICE_UUID);

    g_chr_state = svc->createCharacteristic(
        CATAN_BLE_STATE_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    g_chr_state->setCallbacks(new StateCb());

    g_chr_input = svc->createCharacteristic(
        CATAN_BLE_INPUT_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    g_chr_input->setCallbacks(new InputCb());

    g_chr_identity = svc->createCharacteristic(
        CATAN_BLE_IDENTITY_UUID,
        NIMBLE_PROPERTY::WRITE);
    g_chr_identity->setCallbacks(new IdentityCb());

    g_chr_slot = svc->createCharacteristic(
        CATAN_BLE_SLOT_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    uint8_t no_slot = slots::NO_SLOT;
    g_chr_slot->setValue(&no_slot, 1);

    svc->start();

    writeAdvertisingPayload();
    startAdvertisingIfRoom();
    LOGI("BLE", "advertising as '%s' (max %u conns)",
         CATAN_BLE_DEVICE_NAME, (unsigned)MAX_BLE_CONNECTIONS);
}

void broadcastBoardState(const uint8_t* payload, size_t len) {
    if (!g_chr_state || len == 0) return;
    g_chr_state->setValue(payload, len);
    if (g_server && g_server->getConnectedCount() > 0) {
        g_chr_state->notify();
    }
}

void tick() {
    const uint32_t now = millis();
    for (auto& p : g_pending) {
        if (!p.active) continue;
        if (slots::slotForConn(p.conn_handle) != slots::NO_SLOT) {
            // Already claimed since we last looked — clear.
            p.active = false;
            continue;
        }
        if (now - p.connected_at_ms >= SLOT_CLAIM_TIMEOUT_MS) {
            LOGW("BLE", "conn=%u never sent Identity — disconnecting",
                 (unsigned)p.conn_handle);
            if (g_server) g_server->disconnect(p.conn_handle);
            p.active = false;
        }
    }
}

}  // namespace ble_hub
