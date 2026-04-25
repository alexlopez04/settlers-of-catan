// =============================================================================
// main.cpp — Catan BLE hub (ESP32-C6).
//
//   Mega  <--UART(Serial1, 115200)-->  ESP32-C6 hub  <--BLE-->  up to 4 phones
//
// Responsibilities:
//   - Accept up to MAX_BLE_CONNECTIONS concurrent BLE centrals.
//   - On Identity write, claim/restore a stable seat (0..3) per client_id;
//     mapping persists in NVS across reboots.
//   - Forward BLE PlayerInput → UART (PlayerInput frame).
//   - Whenever the seat occupancy changes, push a PlayerPresence frame to
//     the Mega so it can update the lobby.
//   - Forward UART BoardState → BLE notify (and cache the latest frame for
//     read).
// =============================================================================

#include <Arduino.h>

#include "config.h"
#include "catan_log.h"
#include "catan_wire.h"
#include "link.h"
#include "ble_hub.h"
#include "player_slots.h"
#include "proto/catan.pb.h"

namespace {

uint32_t last_heartbeat_ms = 0;
uint32_t last_presence_ms  = 0;
uint8_t  last_presence_mask = 0xFF;   // forces an initial emit

// ── Outbound: PlayerInput (BLE -> UART) ────────────────────────────────────
void onBleInput(const catan_PlayerInput& in) {
    uint8_t payload[CATAN_MAX_PAYLOAD];
    size_t  n = catan_encode_player_input(&in, payload, sizeof(payload));
    if (n == 0) {
        LOGE("HUB", "encode PlayerInput fail");
        return;
    }
    bool ok = mega_link::send(CATAN_MSG_PLAYER_INPUT, payload, (uint16_t)n);
    LOGI("HUB", "input -> Mega slot=%u action=%u%s",
         (unsigned)in.player_id, (unsigned)in.action, ok ? "" : " [DROPPED]");
}

// ── Outbound: PlayerPresence (BLE conn change -> UART) ─────────────────────
void sendPresence() {
    catan_PlayerPresence pres = catan_PlayerPresence_init_zero;
    pres.proto_version  = CATAN_PROTO_VERSION;
    pres.connected_mask = slots::connectedMask();

    pres.client_ids_count = MAX_PLAYERS;
    const auto* tbl = slots::table();
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (tbl[i].occupied && tbl[i].client_id[0] != '\0') {
            strncpy(pres.client_ids[i], tbl[i].client_id,
                    sizeof(pres.client_ids[i]) - 1);
            pres.client_ids[i][sizeof(pres.client_ids[i]) - 1] = '\0';
        } else {
            pres.client_ids[i][0] = '\0';
        }
    }

    uint8_t payload[CATAN_MAX_PAYLOAD];
    size_t  n = catan_encode_player_presence(&pres, payload, sizeof(payload));
    if (n == 0) {
        LOGE("HUB", "encode PlayerPresence fail");
        return;
    }
    bool ok = mega_link::send(CATAN_MSG_PLAYER_PRESENCE, payload, (uint16_t)n);
    LOGI("HUB", "presence -> Mega mask=0x%02X count=%u%s",
         (unsigned)pres.connected_mask, (unsigned)slots::connectedCount(),
         ok ? "" : " [DROPPED]");
    last_presence_ms   = millis();
    last_presence_mask = pres.connected_mask;
}

void onBlePresenceChanged() { sendPresence(); }

// ── Inbound: UART -> BLE (only BoardState is expected from the Mega) ──────
void onLinkFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    switch (type) {
        case CATAN_MSG_BOARD_STATE: {
            // Validate before pushing so the phone never sees a wrong-version
            // or malformed BoardState.
            catan_BoardState bs = catan_BoardState_init_zero;
            if (!catan_decode_board_state(payload, len, &bs)) {
                LOGW("HUB", "BoardState decode fail (len=%u)", (unsigned)len);
                return;
            }
            ble_hub::broadcastBoardState(payload, len);
            LOGD("HUB", "BoardState -> %u centrals (phase=%u cur=%u)",
                 (unsigned)slots::connectedCount(),
                 (unsigned)bs.phase, (unsigned)bs.current_player);
            break;
        }
        default:
            LOGW("HUB", "unknown frame type=0x%02X len=%u", type, len);
            break;
    }
}

void heartbeat() {
    const auto& s = mega_link::stats();
    LOGI("HB", "conn=%u mask=0x%02X uart rx=%lu fr=%lu bad_crc=%lu bad_mag=%lu tx=%lu drop=%lu",
         (unsigned)slots::connectedCount(),
         (unsigned)slots::connectedMask(),
         (unsigned long)s.rx_bytes,
         (unsigned long)s.rx_frames,
         (unsigned long)s.rx_bad_crc,
         (unsigned long)s.rx_bad_magic,
         (unsigned long)s.tx_frames,
         (unsigned long)s.tx_dropped);
}

}  // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(150);
    Serial.println();
    Serial.println("====================================");
    Serial.printf (" Catan BLE Hub (ESP32-C6)\n");
    Serial.printf (" proto v%u  max conns=%u\n",
                   CATAN_PROTO_VERSION, (unsigned)MAX_BLE_CONNECTIONS);
    Serial.println("====================================");

    slots::init();
    mega_link::init(onLinkFrame);
    ble_hub::init(onBleInput, onBlePresenceChanged);

    // Tell the Mega the current (likely all-zero) presence so it has a
    // reference even before any phone connects.
    sendPresence();

    LOGI("BOOT", "ready");
}

void loop() {
    mega_link::poll();
    ble_hub::tick();

    const uint32_t now = millis();

    // Re-send presence periodically (idempotent) — guards against the Mega
    // missing the first frame during its boot sequence.
    if (now - last_presence_ms >= PRESENCE_RESEND_MS) {
        if (slots::connectedMask() != last_presence_mask ||
            now - last_presence_ms >= 4 * PRESENCE_RESEND_MS) {
            sendPresence();
        } else {
            last_presence_ms = now;
        }
    }

    if (now - last_heartbeat_ms >= HEARTBEAT_MS) {
        last_heartbeat_ms = now;
        heartbeat();
    }
    delay(2);
}
