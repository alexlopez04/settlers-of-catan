// =============================================================================
// comms.cpp — see header.
//
// Implements:
//   - In-memory client_id → seat (0..3) table (session-scoped).
//   - NimBLE multi-connection peripheral (up to MAX_BLE_CONNECTIONS).
//   - PlayerInput hand-off from the NimBLE host task to the game task via
//     a FreeRTOS queue.
//   - Latched "presence changed" flag drained by poll().
// =============================================================================

#include "comms.h"
#include "config.h"
#include "catan_log.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <string.h>

namespace {

// ── Slot table ─────────────────────────────────────────────────────────────

constexpr uint8_t  NO_SLOT       = 0xFF;
constexpr uint16_t NO_CONN       = 0xFFFF;
constexpr size_t   CLIENT_ID_MAX = 40;   // matches catan.options

struct Slot {
    bool     occupied;
    uint16_t conn_handle;
    char     client_id[CLIENT_ID_MAX];
};

Slot                g_slots[MAX_PLAYERS] = {};
portMUX_TYPE        g_slot_mux           = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool>   g_presence_dirty{false};

uint8_t lookupSlotLocked(const char* client_id) {
    if (!client_id || !client_id[0]) return NO_SLOT;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (strncmp(g_slots[i].client_id, client_id, CLIENT_ID_MAX) == 0) return i;
    }
    return NO_SLOT;
}

uint8_t claimSlot(const char* client_id, uint16_t conn) {
    if (!client_id || !client_id[0]) return NO_SLOT;
    uint8_t slot = NO_SLOT;
    portENTER_CRITICAL(&g_slot_mux);
    slot = lookupSlotLocked(client_id);
    if (slot == NO_SLOT) {
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (g_slots[i].client_id[0] == '\0') { slot = i; break; }
        }
    }
    if (slot == NO_SLOT) {
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (!g_slots[i].occupied) { slot = i; break; }
        }
    }
    if (slot != NO_SLOT) {
        strncpy(g_slots[slot].client_id, client_id, CLIENT_ID_MAX - 1);
        g_slots[slot].client_id[CLIENT_ID_MAX - 1] = '\0';
        g_slots[slot].occupied    = true;
        g_slots[slot].conn_handle = conn;
    }
    portEXIT_CRITICAL(&g_slot_mux);
    if (slot != NO_SLOT) g_presence_dirty.store(true, std::memory_order_relaxed);
    return slot;
}

uint8_t releaseSlot(uint16_t conn) {
    uint8_t freed = NO_SLOT;
    portENTER_CRITICAL(&g_slot_mux);
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (g_slots[i].occupied && g_slots[i].conn_handle == conn) {
            g_slots[i].occupied    = false;
            g_slots[i].conn_handle = NO_CONN;
            freed = i;
            break;
        }
    }
    portEXIT_CRITICAL(&g_slot_mux);
    if (freed != NO_SLOT) g_presence_dirty.store(true, std::memory_order_relaxed);
    return freed;
}

uint8_t slotForConn(uint16_t conn) {
    uint8_t s = NO_SLOT;
    portENTER_CRITICAL(&g_slot_mux);
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (g_slots[i].occupied && g_slots[i].conn_handle == conn) { s = i; break; }
    }
    portEXIT_CRITICAL(&g_slot_mux);
    return s;
}

uint8_t connectedMaskLocked_() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (g_slots[i].occupied) m |= (1u << i);
    return m;
}

uint8_t connectedMaskNow() {
    uint8_t m;
    portENTER_CRITICAL(&g_slot_mux);
    m = connectedMaskLocked_();
    portEXIT_CRITICAL(&g_slot_mux);
    return m;
}

// ── BLE state ──────────────────────────────────────────────────────────────

NimBLEServer*         g_server      = nullptr;
NimBLECharacteristic* g_chr_state   = nullptr;
NimBLECharacteristic* g_chr_input   = nullptr;
NimBLECharacteristic* g_chr_ident   = nullptr;
NimBLECharacteristic* g_chr_slot    = nullptr;

QueueHandle_t         g_input_queue = nullptr;
constexpr UBaseType_t INPUT_QUEUE_LEN = 16;

comms::Stats g_stats = {};

struct PendingClaim {
    bool     active;
    uint16_t conn_handle;
    uint32_t connected_at_ms;
};
PendingClaim g_pending[MAX_BLE_CONNECTIONS] = {};

void trackPending(uint16_t conn) {
    for (auto& p : g_pending) {
        if (!p.active) {
            p.active = true;
            p.conn_handle = conn;
            p.connected_at_ms = millis();
            return;
        }
    }
}

void clearPending(uint16_t conn) {
    for (auto& p : g_pending) {
        if (p.active && p.conn_handle == conn) { p.active = false; return; }
    }
}

void notifySlot(uint16_t conn, uint8_t slot) {
    if (!g_chr_slot) return;
    g_chr_slot->setValue(&slot, 1);
    g_chr_slot->notify(conn);
}

void writeAdvertisingPayload() {
    NimBLEAdvertisementData advData;
    advData.setCompleteServices(NimBLEUUID(CATAN_BLE_SERVICE_UUID));

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

class IdentityCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        std::string v = c->getValue();
        if (v.empty() || v.size() >= CLIENT_ID_MAX) {
            LOGW("BLE", "Identity bad len=%u", (unsigned)v.size());
            return;
        }
        for (char ch : v) {
            if (ch < 0x20 || ch > 0x7E) {
                LOGW("BLE", "Identity non-printable byte");
                return;
            }
        }
        char client_id[CLIENT_ID_MAX];
        memcpy(client_id, v.data(), v.size());
        client_id[v.size()] = '\0';

        uint16_t conn = info.getConnHandle();
        uint8_t slot = claimSlot(client_id, conn);
        notifySlot(conn, slot);
        if (slot == NO_SLOT) {
            LOGW("BLE", "no slot for client '%s' — disconnecting conn=%u",
                 client_id, (unsigned)conn);
            g_server->disconnect(conn);
            return;
        }
        clearPending(conn);
        LOGI("BLE", "slot %u <- conn=%u client='%s'",
             (unsigned)slot, (unsigned)conn, client_id);
    }
};

class InputCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        uint16_t conn = info.getConnHandle();
        uint8_t  slot = slotForConn(conn);
        if (slot == NO_SLOT) {
            g_stats.inputs_dropped++;
            LOGW("BLE", "Input from un-seated conn=%u", (unsigned)conn);
            return;
        }
        std::string v = c->getValue();
        if (v.empty() || v.size() > CATAN_MAX_PAYLOAD) {
            g_stats.inputs_dropped++;
            LOGW("BLE", "Input bad len=%u slot=%u", (unsigned)v.size(), (unsigned)slot);
            return;
        }
        catan_PlayerInput in = catan_PlayerInput_init_zero;
        if (!catan_decode_player_input(
                reinterpret_cast<const uint8_t*>(v.data()), v.size(), &in)) {
            g_stats.inputs_dropped++;
            LOGW("BLE", "Input decode fail slot=%u len=%u",
                 (unsigned)slot, (unsigned)v.size());
            return;
        }
        // Authoritative re-stamp so downstream code can trust these fields.
        in.proto_version = CATAN_PROTO_VERSION;
        in.player_id     = slot;

        if (xQueueSend(g_input_queue, &in, 0) != pdTRUE) {
            g_stats.inputs_dropped++;
            LOGW("BLE", "Input queue full — dropped slot=%u action=%u",
                 (unsigned)slot, (unsigned)in.action);
            return;
        }
        g_stats.inputs_rx++;
    }
};

class StateCb : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info,
                     uint16_t sub_value) override {
        LOGI("BLE", "State subscribed by conn=%u value=0x%04X",
             (unsigned)info.getConnHandle(), (unsigned)sub_value);
    }
};

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
        uint16_t conn = info.getConnHandle();
        g_stats.connect_events++;
        LOGI("BLE", "connect conn=%u (n=%u)",
             (unsigned)conn, (unsigned)g_server->getConnectedCount());
        trackPending(conn);
        notifySlot(conn, NO_SLOT);
        startAdvertisingIfRoom();
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
        uint16_t conn = info.getConnHandle();
        g_stats.disconnect_events++;
        clearPending(conn);
        uint8_t freed = releaseSlot(conn);
        LOGI("BLE", "disconnect conn=%u reason=%d freed=%u",
             (unsigned)conn, reason, (unsigned)freed);
        startAdvertisingIfRoom();
    }
};

}  // namespace

namespace comms {

void init() {
    g_input_queue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(catan_PlayerInput));

    for (auto& s : g_slots) {
        s.occupied = false;
        s.conn_handle = NO_CONN;
        s.client_id[0] = '\0';
    }

    NimBLEDevice::init(CATAN_BLE_DEVICE_NAME);
    NimBLEDevice::setPower(9);
    NimBLEDevice::setMTU(BLE_MTU);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(new ServerCb());
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

    g_chr_ident = svc->createCharacteristic(
        CATAN_BLE_IDENTITY_UUID,
        NIMBLE_PROPERTY::WRITE);
    g_chr_ident->setCallbacks(new IdentityCb());

    g_chr_slot = svc->createCharacteristic(
        CATAN_BLE_SLOT_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    uint8_t no_slot = NO_SLOT;
    g_chr_slot->setValue(&no_slot, 1);

    svc->start();

    writeAdvertisingPayload();
    startAdvertisingIfRoom();

    LOGI("BLE", "advertising as '%s' (max %u conns, MTU %u)",
         CATAN_BLE_DEVICE_NAME, (unsigned)MAX_BLE_CONNECTIONS, (unsigned)BLE_MTU);
}

void poll(InputHandler on_input, PresenceHandler on_presence) {
    if (g_input_queue && on_input) {
        catan_PlayerInput in;
        while (xQueueReceive(g_input_queue, &in, 0) == pdTRUE) {
            on_input(in);
        }
    }
    if (on_presence && g_presence_dirty.exchange(false, std::memory_order_relaxed)) {
        g_stats.presence_events++;
        on_presence(connectedMaskNow());
    }
}

void broadcastBoardState(const uint8_t* payload, size_t len) {
    if (!g_chr_state || len == 0) return;
    g_chr_state->setValue(payload, len);
    if (g_server && g_server->getConnectedCount() > 0) {
        g_chr_state->notify();
        g_stats.state_notified++;
    }
}

uint8_t connectedCount() {
    uint8_t n = 0;
    portENTER_CRITICAL(&g_slot_mux);
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) if (g_slots[i].occupied) ++n;
    portEXIT_CRITICAL(&g_slot_mux);
    return n;
}

uint8_t connectedMask() { return connectedMaskNow(); }

void tick() {
    const uint32_t now = millis();
    for (auto& p : g_pending) {
        if (!p.active) continue;
        if (slotForConn(p.conn_handle) != NO_SLOT) { p.active = false; continue; }
        if (now - p.connected_at_ms >= SLOT_CLAIM_TIMEOUT_MS) {
            LOGW("BLE", "conn=%u never sent Identity — disconnecting",
                 (unsigned)p.conn_handle);
            if (g_server) g_server->disconnect(p.conn_handle);
            p.active = false;
        }
    }
}

const Stats& stats() { return g_stats; }

void getSlotClientIds(char out[MAX_PLAYERS][40]) {
    portENTER_CRITICAL(&g_slot_mux);
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i)
        strncpy(out[i], g_slots[i].client_id, 40);
    portEXIT_CRITICAL(&g_slot_mux);
}

void restoreSlotClientIds(const char ids[MAX_PLAYERS][40]) {
    // Snapshot currently-connected devices before rebuilding the slot table,
    // so that any device already seated (e.g. the one that just sent RESUME_YES)
    // is moved to its correct saved slot rather than kept in slot 0.
    struct ConnSnap {
        bool     valid        = false;
        uint16_t conn_handle  = NO_CONN;
        char     client_id[CLIENT_ID_MAX] = {};
    };
    ConnSnap prev[MAX_PLAYERS];

    struct RenotifyEntry { uint16_t conn_handle; uint8_t slot; };
    RenotifyEntry renotify[MAX_PLAYERS] = {};
    uint8_t renotify_count = 0;

    portENTER_CRITICAL(&g_slot_mux);

    // 1. Snapshot currently-occupied slots.
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (g_slots[i].occupied) {
            prev[i].valid = true;
            prev[i].conn_handle = g_slots[i].conn_handle;
            strncpy(prev[i].client_id, g_slots[i].client_id, CLIENT_ID_MAX - 1);
            prev[i].client_id[CLIENT_ID_MAX - 1] = '\0';
        }
    }

    // 2. Rebuild the slot table from saved IDs (all unoccupied).
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        g_slots[i].occupied    = false;
        g_slots[i].conn_handle = NO_CONN;
        strncpy(g_slots[i].client_id, ids[i], CLIENT_ID_MAX - 1);
        g_slots[i].client_id[CLIENT_ID_MAX - 1] = '\0';
    }

    // 3. Re-seat each previously-connected device into its saved slot.
    for (uint8_t j = 0; j < MAX_PLAYERS; ++j) {
        if (!prev[j].valid) continue;
        // Look up the saved slot for this device (table is already populated).
        uint8_t new_slot = lookupSlotLocked(prev[j].client_id);
        if (new_slot == NO_SLOT) {
            // Device not in saved state — give it the first unclaimed slot.
            for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                if (!g_slots[i].occupied && g_slots[i].client_id[0] == '\0') {
                    new_slot = i;
                    strncpy(g_slots[i].client_id, prev[j].client_id, CLIENT_ID_MAX - 1);
                    g_slots[i].client_id[CLIENT_ID_MAX - 1] = '\0';
                    break;
                }
            }
        }
        if (new_slot == NO_SLOT) {
            // Last resort: first unoccupied slot.
            for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                if (!g_slots[i].occupied) { new_slot = i; break; }
            }
        }
        if (new_slot != NO_SLOT && renotify_count < MAX_PLAYERS) {
            g_slots[new_slot].occupied    = true;
            g_slots[new_slot].conn_handle = prev[j].conn_handle;
            renotify[renotify_count++] = { prev[j].conn_handle, new_slot };
        }
    }

    g_presence_dirty.store(true, std::memory_order_relaxed);
    portEXIT_CRITICAL(&g_slot_mux);

    // 4. Notify each re-seated device of its (possibly changed) slot number.
    //    Must happen outside the critical section to avoid BLE blocking.
    for (uint8_t k = 0; k < renotify_count; ++k)
        notifySlot(renotify[k].conn_handle, renotify[k].slot);
}

}  // namespace comms
