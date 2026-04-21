// =============================================================================
// main.cpp — Settlers of Catan: ESP32-C6 Player Station
//
// Receives BoardToPlayer protobuf messages from the central Arduino Mega via
// I2C (slave mode).  Displays game state on a bit-banged SSD1306 OLED.
// Three physical buttons and a BLE mobile interface provide player input.
//
// Hardware:
//   - I2C slave (GPIO6=SDA, GPIO7=SCL) connected to Arduino Mega master
//   - SSD1306 128×64 OLED (GPIO2=SDA, GPIO3=SCL) via bit-bang I2C
//   - 3 buttons: Left=GPIO10, Center=GPIO11, Right=GPIO12 (active-low)
//
// I2C wire-frame format (both directions):
//   [0xCA] [payload_len: uint8] [nanopb_payload: payload_len bytes]
// =============================================================================

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2c_slave.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "proto/catan.pb.h"
#include "bt_manager.h"

static const char* TAG = "player";

// ── Configuration — CHANGE PER DEVICE ───────────────────────────────────────
#define MY_PLAYER_ID    1           // 0-3 for each player station
#define MY_ADDRESS      (0x10 + MY_PLAYER_ID)

// ── I2C wire-frame ───────────────────────────────────────────────────────────
#define CATAN_WIRE_MAGIC   0xCA
#define CATAN_FRAME_HEADER 2        // magic byte + length byte

// ── I2C slave bus (to Arduino Mega) ─────────────────────────────────────────
#define MEGA_SDA        6
#define MEGA_SCL        7
#define I2C_BUF_SIZE    200

// ── OLED bit-bang ───────────────────────────────────────────────────────────
#define OLED_SDA        2
#define OLED_SCL        3
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define I2C_DELAY_US    5

// ── Buttons ─────────────────────────────────────────────────────────────────
#define BTN_LEFT_PIN    10
#define BTN_CENTER_PIN  11
#define BTN_RIGHT_PIN   12
#define DEBOUNCE_MS     50

// ── Globals ─────────────────────────────────────────────────────────────────
static i2c_slave_dev_handle_t slave_handle = NULL;
static QueueHandle_t  rx_queue;
static SemaphoreHandle_t tx_mutex;
static uint8_t fb[OLED_WIDTH * OLED_HEIGHT / 8];

// Latest game state from the board
static catan_BoardToPlayer g_state;
static SemaphoreHandle_t state_mutex;

// Pending input to send back to the board.
// Physical buttons set g_pending_button; BLE/semantic commands set
// g_pending_action.  Both are cleared after encode_response() consumes them.
static volatile catan_ButtonId   g_pending_button = catan_ButtonId_BTN_NONE;
static volatile catan_PlayerAction g_pending_action = catan_PlayerAction_ACTION_NONE;
static portMUX_TYPE g_pending_mux = portMUX_INITIALIZER_UNLOCKED;

// Pre-encoded response buffer with wire-frame header (protected by tx_mutex).
// Layout: [0xCA][len][pb_bytes...]
static uint8_t  g_tx_buf[CATAN_FRAME_HEADER + catan_PlayerToBoard_size];
static size_t   g_tx_len = 0;

// =====================================================================
// Font — 5×7 pixel glyphs for OLED rendering
// Supports: A-Z, 0-9, space, and common punctuation
// =====================================================================

// Character index lookup: returns -1 for unsupported chars
static int charIndex(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';        // 0-25
    if (c >= 'a' && c <= 'z') return c - 'a';         // same as uppercase
    if (c >= '0' && c <= '9') return 26 + (c - '0');  // 26-35
    switch (c) {
        case ' ':  return 36;
        case ':':  return 37;
        case '+':  return 38;
        case '=':  return 39;
        case '-':  return 40;
        case '/':  return 41;
        case '!':  return 42;
        case '.':  return 43;
        case ',':  return 44;
        case '?':  return 45;
        case '>':  return 46;
        case '<':  return 47;
        default:   return 36; // space for unknown
    }
}

// 5-byte column data per glyph (each byte is a column, LSB=top)
static const uint8_t font5x7[][5] = {
    // A-Z (indices 0-25)
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z

    // 0-9 (indices 26-35)
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9

    // Symbols (indices 36-47)
    {0x00,0x00,0x00,0x00,0x00}, // space (36)
    {0x00,0x36,0x36,0x00,0x00}, // :     (37)
    {0x08,0x08,0x3E,0x08,0x08}, // +     (38)
    {0x14,0x14,0x14,0x14,0x14}, // =     (39)
    {0x08,0x08,0x08,0x08,0x08}, // -     (40)
    {0x20,0x10,0x08,0x04,0x02}, // /     (41)
    {0x00,0x00,0x5F,0x00,0x00}, // !     (42)
    {0x00,0x60,0x60,0x00,0x00}, // .     (43)
    {0x00,0x80,0x60,0x00,0x00}, // ,     (44)
    {0x02,0x01,0x51,0x09,0x06}, // ?     (45)
    {0x41,0x22,0x14,0x08,0x00}, // >     (46)
    {0x00,0x08,0x14,0x22,0x41}, // <     (47)
};

// =====================================================================
// Framebuffer drawing
// =====================================================================

static void draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int idx = x + (y / 8) * OLED_WIDTH;
    if (on) fb[idx] |=  (1 << (y % 8));
    else    fb[idx] &= ~(1 << (y % 8));
}

static void draw_char(int x, int y, char c) {
    int idx = charIndex(c);
    if (idx < 0) return;
    for (int col = 0; col < 5; col++) {
        uint8_t coldata = font5x7[idx][col];
        for (int row = 0; row < 7; row++)
            draw_pixel(x + col, y + row, (coldata >> row) & 1);
    }
}

static void draw_string(int x, int y, const char* str) {
    while (*str) { draw_char(x, y, *str++); x += 6; }
}

// Draw string right-aligned at the given Y, ending at x_right
static void draw_string_right(int x_right, int y, const char* str) {
    int len = strlen(str);
    int x = x_right - (len * 6);
    if (x < 0) x = 0;
    draw_string(x, y, str);
}

// Draw string centered at the given Y
static void draw_string_center(int y, const char* str) {
    int len = strlen(str);
    int x = (OLED_WIDTH - len * 6) / 2;
    if (x < 0) x = 0;
    draw_string(x, y, str);
}

// Draw a horizontal line
static void draw_hline(int y) {
    for (int x = 0; x < OLED_WIDTH; x++) draw_pixel(x, y, true);
}

static void oled_clear() { memset(fb, 0, sizeof(fb)); }

// =====================================================================
// Bit-bang I2C + OLED driver
// =====================================================================

static void sda_high() { gpio_set_level((gpio_num_t)OLED_SDA, 1); }
static void sda_low()  { gpio_set_level((gpio_num_t)OLED_SDA, 0); }
static void scl_high() { gpio_set_level((gpio_num_t)OLED_SCL, 1); }
static void scl_low()  { gpio_set_level((gpio_num_t)OLED_SCL, 0); }
static void i2c_delay(){ ets_delay_us(I2C_DELAY_US); }

static void i2c_start() {
    sda_high(); scl_high(); i2c_delay();
    sda_low();  i2c_delay();
    scl_low();  i2c_delay();
}

static void i2c_stop() {
    sda_low();  scl_high(); i2c_delay();
    sda_high(); i2c_delay();
}

static bool i2c_write_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if ((byte >> i) & 1) sda_high(); else sda_low();
        i2c_delay();
        scl_high(); i2c_delay();
        scl_low();  i2c_delay();
    }
    gpio_set_direction((gpio_num_t)OLED_SDA, GPIO_MODE_INPUT);
    i2c_delay();
    scl_high(); i2c_delay();
    bool ack = (gpio_get_level((gpio_num_t)OLED_SDA) == 0);
    scl_low();  i2c_delay();
    gpio_set_direction((gpio_num_t)OLED_SDA, GPIO_MODE_OUTPUT);
    return ack;
}

static void oled_write_cmd(uint8_t cmd) {
    i2c_start();
    i2c_write_byte(OLED_ADDR << 1);
    i2c_write_byte(0x00);
    i2c_write_byte(cmd);
    i2c_stop();
}

static void oled_write_data(uint8_t *data, size_t len) {
    i2c_start();
    i2c_write_byte(OLED_ADDR << 1);
    i2c_write_byte(0x40);
    for (size_t i = 0; i < len; i++) i2c_write_byte(data[i]);
    i2c_stop();
}

static void oled_flush() {
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(7);
    oled_write_data(fb, sizeof(fb));
}

static void oled_init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_SDA) | (1ULL << OLED_SCL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    sda_high(); scl_high();
    vTaskDelay(pdMS_TO_TICKS(10));

    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++)
        oled_write_cmd(init_cmds[i]);

    ESP_LOGI(TAG, "OLED ready (bit-bang SDA=%d SCL=%d)", OLED_SDA, OLED_SCL);
}

// =====================================================================
// Buttons — debounced polling
// =====================================================================

struct BtnState {
    gpio_num_t pin;
    bool last_stable;
    bool last_raw;
    int64_t last_change_us;
    bool pressed;   // one-shot flag
};

static BtnState g_btns[3];

static void buttons_init() {
    gpio_num_t pins[3] = {
        (gpio_num_t)BTN_LEFT_PIN,
        (gpio_num_t)BTN_CENTER_PIN,
        (gpio_num_t)BTN_RIGHT_PIN
    };
    for (int i = 0; i < 3; i++) {
        g_btns[i].pin = pins[i];
        g_btns[i].last_stable = true;
        g_btns[i].last_raw = true;
        g_btns[i].last_change_us = 0;
        g_btns[i].pressed = false;

        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << pins[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }
}

static void buttons_poll() {
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < 3; i++) {
        bool raw = gpio_get_level(g_btns[i].pin);
        if (raw != g_btns[i].last_raw) {
            g_btns[i].last_change_us = now;
            g_btns[i].last_raw = raw;
        }
        if ((now - g_btns[i].last_change_us) > (DEBOUNCE_MS * 1000LL)) {
            if (raw != g_btns[i].last_stable) {
                g_btns[i].last_stable = raw;
                if (!raw) {  // Active low
                    g_btns[i].pressed = true;
                }
            }
        }
    }
}

static catan_ButtonId consume_button() {
    for (int i = 0; i < 3; i++) {
        if (g_btns[i].pressed) {
            g_btns[i].pressed = false;
            switch (i) {
                case 0: return catan_ButtonId_BTN_LEFT;
                case 1: return catan_ButtonId_BTN_CENTER;
                case 2: return catan_ButtonId_BTN_RIGHT;
            }
        }
    }
    return catan_ButtonId_BTN_NONE;
}

// =====================================================================
// Map a semantic PlayerAction to a ButtonId based on the current
// game state.  Used when a BLE client sends an ACTION_* command so
// that the board receives the equivalent BTN_* it already understands.
// =====================================================================

static catan_ButtonId action_to_button(catan_PlayerAction action) {
    switch (action) {
        // Direct button equivalents
        case catan_PlayerAction_ACTION_BTN_LEFT:    return catan_ButtonId_BTN_LEFT;
        case catan_PlayerAction_ACTION_BTN_CENTER:  return catan_ButtonId_BTN_CENTER;
        case catan_PlayerAction_ACTION_BTN_RIGHT:   return catan_ButtonId_BTN_RIGHT;

        // Semantic → button mapping (matches the labels sent in BoardToPlayer)
        case catan_PlayerAction_ACTION_ROLL_DICE:   return catan_ButtonId_BTN_LEFT;
        case catan_PlayerAction_ACTION_END_TURN:    return catan_ButtonId_BTN_RIGHT;
        case catan_PlayerAction_ACTION_TRADE:       return catan_ButtonId_BTN_LEFT;
        case catan_PlayerAction_ACTION_SKIP_ROBBER: return catan_ButtonId_BTN_CENTER;
        case catan_PlayerAction_ACTION_PLACE_DONE:  return catan_ButtonId_BTN_CENTER;
        case catan_PlayerAction_ACTION_START_GAME:  return catan_ButtonId_BTN_LEFT;
        case catan_PlayerAction_ACTION_NEXT_NUMBER: return catan_ButtonId_BTN_CENTER;

        default: return catan_ButtonId_BTN_NONE;
    }
}

// =====================================================================
// Encode pending button / action response.
// Pre-encodes the wire frame [0xCA][len][pb_bytes] into g_tx_buf so
// the I2C transmit task can immediately satisfy a master read.
// =====================================================================

static void encode_response() {
    catan_PlayerToBoard msg = catan_PlayerToBoard_init_zero;

    // Atomically consume both pending inputs (button has priority).
    taskENTER_CRITICAL(&g_pending_mux);
    catan_ButtonId   btn    = g_pending_button;
    catan_PlayerAction act  = g_pending_action;
    g_pending_button = catan_ButtonId_BTN_NONE;
    g_pending_action = catan_PlayerAction_ACTION_NONE;
    taskEXIT_CRITICAL(&g_pending_mux);

    if (btn != catan_ButtonId_BTN_NONE) {
        msg.type   = catan_MsgType_MSG_BUTTON_EVENT;
        msg.button = btn;
    } else if (act != catan_PlayerAction_ACTION_NONE) {
        msg.type   = catan_MsgType_MSG_ACTION;
        msg.action = act;
        msg.button = action_to_button(act);   // backward-compat for board
    } else {
        msg.type = catan_MsgType_MSG_PLAYER_READY;
    }

    xSemaphoreTake(tx_mutex, portMAX_DELAY);
    // Encode into g_tx_buf[2..] to leave room for the 2-byte frame header.
    pb_ostream_t stream = pb_ostream_from_buffer(
        g_tx_buf + CATAN_FRAME_HEADER,
        sizeof(g_tx_buf) - CATAN_FRAME_HEADER);
    if (pb_encode(&stream, catan_PlayerToBoard_fields, &msg)) {
        g_tx_buf[0] = CATAN_WIRE_MAGIC;
        g_tx_buf[1] = (uint8_t)stream.bytes_written;
        // Zero-pad remainder so every write is exactly sizeof(g_tx_buf) bytes.
        // This matches the fixed request_len used by the board master and prevents
        // TX ring buffer de-alignment when multiple writes accumulate.
        memset(g_tx_buf + CATAN_FRAME_HEADER + stream.bytes_written, 0,
               sizeof(g_tx_buf) - CATAN_FRAME_HEADER - stream.bytes_written);
        g_tx_len = sizeof(g_tx_buf);
    } else {
        g_tx_len = 0;
    }
    xSemaphoreGive(tx_mutex);
}

// =====================================================================
// I2C slave — receive callback (master write → us)
// =====================================================================

typedef struct {
    uint8_t data[I2C_BUF_SIZE];
    uint32_t len;
} RxMsg;

static bool on_receive(i2c_slave_dev_handle_t handle,
                       const i2c_slave_rx_done_event_data_t *event,
                       void *arg) {
    QueueHandle_t q = (QueueHandle_t)arg;
    if (event->length < CATAN_FRAME_HEADER) return false;

    const uint8_t *buf = event->buffer;
    if (buf[0] != CATAN_WIRE_MAGIC) return false;   // discard bad frames

    uint8_t payload_len = buf[1];
    uint32_t total = CATAN_FRAME_HEADER + payload_len;
    if (total > event->length || payload_len == 0) return false;

    RxMsg rm;
    rm.len = payload_len < I2C_BUF_SIZE ? payload_len : I2C_BUF_SIZE;
    memcpy(rm.data, buf + CATAN_FRAME_HEADER, rm.len);

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(q, &rm, &woken);
    return woken == pdTRUE;
}

static void mega_slave_init() {
    i2c_slave_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)MEGA_SDA,
        .scl_io_num = (gpio_num_t)MEGA_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = 64,
        .receive_buf_depth = 256,
        .slave_addr = MY_ADDRESS,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
        .intr_priority = 0,
        .flags = { .allow_pd = 0 },
    };
    ESP_ERROR_CHECK(i2c_new_slave_device(&config, &slave_handle));

    i2c_slave_event_callbacks_t cbs = {
        .on_receive = on_receive,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(slave_handle, &cbs, rx_queue));
    ESP_LOGI(TAG, "I2C slave ready addr=0x%02X", MY_ADDRESS);
}

// =====================================================================
// Display rendering — build framebuffer from game state
// =====================================================================

static const char* res_labels[] = { "L", "W", "G", "B", "O" };

static void render_display(const catan_BoardToPlayer* s) {
    oled_clear();

    switch (s->phase) {
        case catan_GamePhase_PHASE_WAITING_FOR_PLAYERS: {
            draw_string_center(0, "SETTLERS OF CATAN");
            draw_hline(10);
            char buf[22];
            snprintf(buf, sizeof(buf), "PLAYER %u", s->your_player_id + 1);
            draw_string_center(16, buf);
            snprintf(buf, sizeof(buf), "%u CONNECTED", s->num_players);
            draw_string_center(28, buf);
            if (s->num_players >= 2) {
                draw_string_center(42, "PRESS TO START");
            } else {
                draw_string_center(42, "WAITING...");
            }
            break;
        }

        case catan_GamePhase_PHASE_BOARD_SETUP:
            draw_string_center(10, "BOARD SETUP");
            draw_string_center(30, "RANDOMIZING...");
            break;

        case catan_GamePhase_PHASE_NUMBER_REVEAL: {
            draw_string_center(0, "NUMBER REVEAL");
            draw_hline(10);
            char buf[22];
            snprintf(buf, sizeof(buf), "NUMBER: %u", (unsigned)s->reveal_number);
            draw_string_center(20, buf);
            // Button bar
            draw_hline(54);
            draw_string_center(56, "NEXT");
            break;
        }

        case catan_GamePhase_PHASE_INITIAL_PLACEMENT: {
            char buf[22];
            snprintf(buf, sizeof(buf), "SETUP ROUND %u", (unsigned)s->setup_round);
            draw_string(0, 0, buf);
            draw_hline(10);

            bool my_turn = (s->current_player == s->your_player_id);
            if (my_turn) {
                draw_string_center(16, "YOUR TURN!");
                draw_string_center(28, "PLACE PIECE");
                draw_string_center(38, "THEN CONFIRM");
            } else {
                snprintf(buf, sizeof(buf), "P%u PLACING...", s->current_player + 1);
                draw_string_center(24, buf);
            }

            draw_hline(54);
            if (my_turn) {
                draw_string_center(56, "DONE");
            }
            break;
        }

        case catan_GamePhase_PHASE_PLAYING:
        case catan_GamePhase_PHASE_TRADE: {
            bool my_turn = (s->current_player == s->your_player_id);

            // Row 0: Player info + VP
            char buf[22];
            uint32_t my_vp = 0;
            switch (s->your_player_id) {
                case 0: my_vp = s->vp0; break;
                case 1: my_vp = s->vp1; break;
                case 2: my_vp = s->vp2; break;
                case 3: my_vp = s->vp3; break;
            }
            snprintf(buf, sizeof(buf), "P%u VP:%u", s->your_player_id + 1, (unsigned)my_vp);
            draw_string(0, 0, buf);

            // Row 1: Whose turn
            if (my_turn) {
                draw_string(0, 10, "YOUR TURN");
            } else {
                snprintf(buf, sizeof(buf), "P%u TURN", s->current_player + 1);
                draw_string(0, 10, buf);
            }

            // Row 2: Dice result
            if (s->has_rolled) {
                snprintf(buf, sizeof(buf), "DICE:%u+%u=%u",
                         (unsigned)s->die1, (unsigned)s->die2, (unsigned)s->dice_total);
                draw_string(0, 20, buf);
            }

            // Row 3: Resources
            snprintf(buf, sizeof(buf), "%s:%u %s:%u %s:%u %s:%u %s:%u",
                     res_labels[0], (unsigned)s->res_lumber,
                     res_labels[1], (unsigned)s->res_wool,
                     res_labels[2], (unsigned)s->res_grain,
                     res_labels[3], (unsigned)s->res_brick,
                     res_labels[4], (unsigned)s->res_ore);
            draw_string(0, 32, buf);

            // Row 4: Display lines from board
            if (s->line1[0]) {
                draw_string(0, 42, s->line1);
            }

            // Button bar
            draw_hline(54);
            if (s->btn_left[0])   draw_string(0, 56, s->btn_left);
            if (s->btn_center[0]) draw_string_center(56, s->btn_center);
            if (s->btn_right[0])  draw_string_right(OLED_WIDTH, 56, s->btn_right);
            break;
        }

        case catan_GamePhase_PHASE_ROBBER: {
            bool my_turn = (s->current_player == s->your_player_id);
            if (my_turn) {
                draw_string_center(10, "MOVE ROBBER!");
                draw_string_center(28, "PLACE ON NEW TILE");
                draw_hline(54);
                draw_string_center(56, "SKIP");
            } else {
                char buf[22];
                snprintf(buf, sizeof(buf), "P%u MOVING ROBBER", s->current_player + 1);
                draw_string_center(20, buf);
            }
            break;
        }

        case catan_GamePhase_PHASE_GAME_OVER: {
            draw_string_center(4, "GAME OVER!");
            draw_hline(14);
            char buf[22];
            if (s->winner_id != 0xFF) {
                snprintf(buf, sizeof(buf), "P%u WINS!", s->winner_id + 1);
                draw_string_center(24, buf);

                if (s->winner_id == s->your_player_id) {
                    draw_string_center(38, "CONGRATULATIONS!");
                } else {
                    draw_string_center(38, "BETTER LUCK");
                    draw_string_center(48, "NEXT TIME!");
                }
            }
            break;
        }

        default:
            draw_string_center(20, "UNKNOWN STATE");
            break;
    }

    oled_flush();
}

// =====================================================================
// I2C transmit task — blocks on i2c_slave_transmit waiting for master read
// =====================================================================

static void tx_task(void* arg) {
    while (true) {
        // Only re-encode if there is new pending input; otherwise keep the
        // previously encoded frame (e.g. a button event) in g_tx_buf so the
        // board can read it.  The unconditional encode was overwriting button
        // events with MSG_PLAYER_READY before the master had a chance to read.
        taskENTER_CRITICAL(&g_pending_mux);
        bool has_pending = (g_pending_button != catan_ButtonId_BTN_NONE ||
                            g_pending_action != catan_PlayerAction_ACTION_NONE);
        taskEXIT_CRITICAL(&g_pending_mux);
        if (has_pending || g_tx_len == 0) {
            encode_response();
        }

        xSemaphoreTake(tx_mutex, portMAX_DELAY);
        uint8_t buf[catan_PlayerToBoard_size + 4];
        size_t len = g_tx_len;
        memcpy(buf, g_tx_buf, len);
        xSemaphoreGive(tx_mutex);

        if (len > 0) {
            // Write response into slave TX buffer for master to read
            uint32_t written = 0;
            esp_err_t err = i2c_slave_write(slave_handle, buf, (uint32_t)len, &written, 500);
            if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "TX error: %s", esp_err_to_name(err));
            }
        }

        // Yield to prevent starving the IDLE task (watchdog reset)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =====================================================================
// BLE command callback — invoked by bt_manager when a mobile client
// writes to the Command characteristic.  Maps the incoming PlayerToBoard
// message into the pending input variables exactly as physical buttons do.
// =====================================================================

static void on_ble_command(const catan_PlayerToBoard *cmd) {
    if (!cmd) return;

    // Prefer the semantic action field when present.
    if (cmd->action != catan_PlayerAction_ACTION_NONE) {
        g_pending_action = cmd->action;
        ESP_LOGI(TAG, "BLE action=%d", cmd->action);
    } else if (cmd->button != catan_ButtonId_BTN_NONE) {
        g_pending_button = cmd->button;
        ESP_LOGI(TAG, "BLE button=%d", cmd->button);
    }
    encode_response();  // pre-encode so the board can read it immediately
}

// =====================================================================
// Main
// =====================================================================

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== Catan Player Station %d ===", MY_PLAYER_ID + 1);

    rx_queue    = xQueueCreate(4, sizeof(RxMsg));
    tx_mutex    = xSemaphoreCreateMutex();
    state_mutex = xSemaphoreCreateMutex();

    g_state = catan_BoardToPlayer_init_zero;
    g_state.your_player_id = MY_PLAYER_ID;

    oled_init();
    buttons_init();
    mega_slave_init();

    // Initialise BLE — must come after NVS is available (handled internally).
    bt_manager_init(MY_PLAYER_ID, on_ble_command);

    // Show startup screen
    oled_clear();
    draw_string_center(4, "SETTLERS OF CATAN");
    draw_hline(14);
    char startup_buf[22];
    snprintf(startup_buf, sizeof(startup_buf), "PLAYER %d", MY_PLAYER_ID + 1);
    draw_string_center(24, startup_buf);
    draw_string_center(38, "CONNECTING...");
    oled_flush();

    // Launch I2C transmit task
    xTaskCreate(tx_task, "tx_task", 4096, NULL, 5, NULL);

    // Main loop: process received messages + poll buttons
    RxMsg rm;
    while (true) {
        // Drain queue — keep only the latest message to avoid re-rendering stale state
        bool got_msg = false;
        while (xQueueReceive(rx_queue, &rm, 0) == pdTRUE) {
            got_msg = true;  // keep draining, rm holds the newest
        }

        if (got_msg) {
            catan_BoardToPlayer msg = catan_BoardToPlayer_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(rm.data, rm.len);
            if (pb_decode(&stream, catan_BoardToPlayer_fields, &msg)) {
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                memcpy(&g_state, &msg, sizeof(g_state));
                xSemaphoreGive(state_mutex);

                // Re-render display and push state to BLE subscribers.
                render_display(&msg);
                bt_manager_notify_state(&msg);
            } else {
                ESP_LOGW(TAG, "Proto decode fail");
            }
        }

        // Poll physical buttons
        buttons_poll();
        catan_ButtonId btn = consume_button();
        if (btn != catan_ButtonId_BTN_NONE) {
            g_pending_button = btn;
            encode_response();
            ESP_LOGI(TAG, "Button: %d", btn);
        }

        // Yield to let IDLE task reset the watchdog
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}