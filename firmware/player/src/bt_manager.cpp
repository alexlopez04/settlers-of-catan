// =============================================================================
// bt_manager.cpp — NimBLE GATT server for the Catan Player Station
//
// Implements the BLE service described in bt_manager.h using the ESP-IDF
// NimBLE stack available on ESP32-C6.
//
// GATT table (static, registered once at boot):
//   Service  CA7A0001  ─┬─ GameState  CA7A0002  READ | NOTIFY
//                       └─ Command    CA7A0003  WRITE | WRITE_NO_RSP
// =============================================================================

#include "bt_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <pb_encode.h>
#include <pb_decode.h>
#include "proto/catan.pb.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "bt_mgr";

// ── UUIDs (128-bit, NimBLE little-endian byte order) ─────────────────────────
//
//  Human-readable form:  CA7A00xx-CA7A-4C4E-8000-00805F9B34FB
//  NimBLE stores UUIDs LSB-first, so the canonical UUID is reversed:
//    FB 34 9B 5F 80 00 | 00 80 | 4E 4C | 7A CA | xx 00 7A CA
//

#define CATAN_UUID_TAIL  \
    0xFB,0x34,0x9B,0x5F,0x80,0x00,  \
    0x00,0x80,                        \
    0x4E,0x4C,                        \
    0x7A,0xCA

static const ble_uuid128_t uuid_svc       = BLE_UUID128_INIT(
    CATAN_UUID_TAIL, 0x01, 0x00, 0x7A, 0xCA);  // CA7A0001
static const ble_uuid128_t uuid_gamestate = BLE_UUID128_INIT(
    CATAN_UUID_TAIL, 0x02, 0x00, 0x7A, 0xCA);  // CA7A0002
static const ble_uuid128_t uuid_command   = BLE_UUID128_INIT(
    CATAN_UUID_TAIL, 0x03, 0x00, 0x7A, 0xCA);  // CA7A0003

// ── Module state ─────────────────────────────────────────────────────────────

static uint8_t           g_player_id   = 0;
static bt_command_cb_t   g_cmd_cb      = NULL;

// BLE connection / subscription tracking (one connection assumed)
static uint16_t          g_conn_handle      = BLE_HS_CONN_HANDLE_NONE;
static uint16_t          g_gamestate_handle = 0;    // ATT value handle
static bool              g_notify_enabled   = false;

// Encoded game state buffer (protected by mutex)
static SemaphoreHandle_t g_state_mutex = NULL;
static uint8_t           g_state_buf[catan_BoardToPlayer_size];
static size_t            g_state_len   = 0;

// Advertised device name, set during init ("Catan-P1" … "Catan-P4")
static char g_dev_name[12];

// ── GAP / advertising ─────────────────────────────────────────────────────────

static void start_advertise(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle    = event->connect.conn_handle;
            g_notify_enabled = false;
            ESP_LOGI(TAG, "Connected conn=%u", g_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connect failed, restarting adv");
            start_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected reason=%d", event->disconnect.reason);
        g_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
        g_notify_enabled = false;
        start_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        // Track notification subscription state for the GameState characteristic.
        if (event->subscribe.attr_handle == g_gamestate_handle) {
            g_notify_enabled = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "GameState notify %s",
                     g_notify_enabled ? "enabled" : "disabled");
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated conn=%u mtu=%u",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

static void start_advertise(void) {
    struct ble_hs_adv_fields fields = {0};

    // Flags: LE-only, limited discoverable
    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name                  = (const uint8_t *)g_dev_name;
    fields.name_len              = (uint8_t)strlen(g_dev_name);
    fields.name_is_complete      = 1;
    fields.uuids128              = &uuid_svc;
    fields.num_uuids128          = 1;
    fields.uuids128_is_complete  = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {
        .conn_mode  = BLE_GAP_CONN_MODE_UND,    // undirected connectable
        .disc_mode  = BLE_GAP_DISC_MODE_GEN,    // general discoverable
        .itvl_min   = BLE_GAP_ADV_ITVL_MS(100),
        .itvl_max   = BLE_GAP_ADV_ITVL_MS(200),
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as \"%s\"", g_dev_name);
    }
}

// ── GATT characteristic access callbacks ─────────────────────────────────────

// GameState: READ access — copy the latest encoded state into the response.
static int gamestate_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    int rc = os_mbuf_append(ctxt->om, g_state_buf, (uint16_t)g_state_len);
    xSemaphoreGive(g_state_mutex);

    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// Command: WRITE access — decode PlayerToBoard and invoke the command callback.
static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    // Flatten the mbuf chain into a contiguous buffer.
    uint16_t payload_len = OS_MBUF_PKTLEN(ctxt->om);
    if (payload_len == 0 || payload_len > (uint16_t)catan_PlayerToBoard_size) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buf[catan_PlayerToBoard_size];
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &copied);
    if (rc != 0 || copied == 0) return BLE_ATT_ERR_UNLIKELY;

    catan_PlayerToBoard cmd = catan_PlayerToBoard_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(buf, copied);
    if (!pb_decode(&stream, catan_PlayerToBoard_fields, &cmd)) {
        ESP_LOGW(TAG, "Command decode failed");
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG, "BLE command type=%d btn=%d action=%d",
             cmd.type, cmd.button, cmd.action);

    if (g_cmd_cb) {
        g_cmd_cb(&cmd);
    }
    return 0;
}

// ── Static GATT service table ─────────────────────────────────────────────────

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // GameState: read + notify
                .uuid       = &uuid_gamestate.u,
                .access_cb  = gamestate_access_cb,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_gamestate_handle,
            },
            {
                // Command: write (with and without response)
                .uuid      = &uuid_command.u,
                .access_cb = command_access_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },  // terminator
        },
    },
    { 0 },  // terminator
};

// ── NimBLE host sync/reset callbacks ─────────────────────────────────────────

static void on_ble_sync(void) {
    // Verify a valid address is available, then start advertising.
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
        return;
    }
    start_advertise();
}

static void on_ble_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset reason=%d", reason);
}

// ── NimBLE host task (runs the NimBLE event loop) ────────────────────────────

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();           // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

// ── Public API ────────────────────────────────────────────────────────────────

void bt_manager_init(uint8_t player_id, bt_command_cb_t on_command) {
    g_player_id = player_id;
    g_cmd_cb    = on_command;

    snprintf(g_dev_name, sizeof(g_dev_name), "Catan-P%u", player_id + 1);

    g_state_mutex = xSemaphoreCreateMutex();
    configASSERT(g_state_mutex);

    // NVS is required by the NimBLE host for IRK/bond storage.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    // Initialise the NimBLE porting layer.
    ESP_ERROR_CHECK(nimble_port_init());

    // Configure the host.
    ble_hs_cfg.sync_cb  = on_ble_sync;
    ble_hs_cfg.reset_cb = on_ble_reset;

    // Register standard GAP + GATT services, then the application service.
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_count_cfg rc=%d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_add_svcs rc=%d", rc);
        return;
    }

    // Set the GAP device name (appears in scan results).
    rc = ble_svc_gap_device_name_set(g_dev_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "gap_device_name_set rc=%d", rc);
    }

    // Start the NimBLE host task on the Bluetooth controller core.
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE manager initialised (player=%u, name=%s)",
             player_id, g_dev_name);
}

void bt_manager_notify_state(const catan_BoardToPlayer *state) {
    if (!state) return;

    // Encode the state into the shared buffer.
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    pb_ostream_t stream = pb_ostream_from_buffer(g_state_buf, sizeof(g_state_buf));
    bool ok = pb_encode(&stream, catan_BoardToPlayer_fields, state);
    if (ok) {
        g_state_len = stream.bytes_written;
    }
    xSemaphoreGive(g_state_mutex);

    if (!ok) {
        ESP_LOGW(TAG, "notify encode failed");
        return;
    }

    // Send GATT notification if a client is subscribed.
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_notify_enabled) {
        return;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(g_state_buf, (uint16_t)g_state_len);
    xSemaphoreGive(g_state_mutex);

    if (!om) {
        ESP_LOGW(TAG, "notify: mbuf alloc failed");
        return;
    }

    int rc = ble_gatts_notify_custom(g_conn_handle, g_gamestate_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify rc=%d", rc);
    }
}

bool bt_manager_connected(void) {
    return (g_conn_handle != BLE_HS_CONN_HANDLE_NONE);
}
