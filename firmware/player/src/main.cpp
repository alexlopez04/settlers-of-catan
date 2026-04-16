#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c_slave.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char* TAG = "main";

// --- Change per device ---
#define MY_ADDRESS      0x10

// --- Mega slave bus ---
#define MEGA_SDA        6
#define MEGA_SCL        7
#define I2C_BUF_SIZE    64

// --- OLED bit-bang ---
#define OLED_SDA        2
#define OLED_SCL        3
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define I2C_DELAY_US    5

// --- Globals ---
static i2c_slave_dev_handle_t slave_handle = NULL;
static QueueHandle_t rx_queue;
static uint8_t fb[OLED_WIDTH * OLED_HEIGHT / 8];

// -------------------------------------------------------
// Font
// -------------------------------------------------------
static const uint8_t font5x7[][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
};

// -------------------------------------------------------
// Drawing
// -------------------------------------------------------
void draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int idx = x + (y / 8) * OLED_WIDTH;
    if (on) fb[idx] |=  (1 << (y % 8));
    else    fb[idx] &= ~(1 << (y % 8));
}

void draw_char(int x, int y, char c) {
    int idx = -1;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
    if (idx < 0) return;
    for (int col = 0; col < 5; col++) {
        uint8_t coldata = font5x7[idx][col];
        for (int row = 0; row < 7; row++)
            draw_pixel(x + col, y + row, (coldata >> row) & 1);
    }
}

void draw_string(int x, int y, const char* str) {
    while (*str) { draw_char(x, y, *str++); x += 6; }
}

void oled_clear() { memset(fb, 0, sizeof(fb)); }

// -------------------------------------------------------
// Bit-bang I2C + OLED
// -------------------------------------------------------
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
        if ((byte >> i) & 1) sda_high();
        else                  sda_low();
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

void oled_flush() {
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(7);
    oled_write_data(fb, sizeof(fb));
}

void oled_init() {
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

// -------------------------------------------------------
// Mega slave (new driver v2)
// -------------------------------------------------------
static bool on_receive(i2c_slave_dev_handle_t handle,
                       const i2c_slave_rx_done_event_data_t *event,
                       void *arg) {
    QueueHandle_t q = (QueueHandle_t)arg;
    char msg[I2C_BUF_SIZE];
    uint32_t len = event->length < I2C_BUF_SIZE ? event->length : I2C_BUF_SIZE - 1;
    memcpy(msg, event->buffer, len);
    msg[len] = '\0';
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(q, msg, &woken);
    return woken == pdTRUE;
}

void mega_slave_init() {
    ESP_LOGI(TAG, "slave init: port=%d sda=%d scl=%d addr=0x%02X",
             I2C_NUM_0, MEGA_SDA, MEGA_SCL, MY_ADDRESS);

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
    ESP_LOGI(TAG, "Mega slave ready addr=0x%02X", MY_ADDRESS);
}

// -------------------------------------------------------
// Main
// -------------------------------------------------------
extern "C" void app_main() {
    rx_queue = xQueueCreate(4, I2C_BUF_SIZE);

    mega_slave_init();
    oled_init();

    oled_clear();
    draw_string(0, 0,  "PLAYER 1");
    draw_string(0, 10, "WAITING FOR");
    draw_string(0, 20, "MEGA");
    oled_flush();

    char msg[I2C_BUF_SIZE];
    while (true) {
        if (xQueueReceive(rx_queue, msg, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Displaying: %s", msg);
            oled_clear();
            draw_string(0, 0,  "PLAYER 1");
            draw_string(0, 10, msg);
            oled_flush();
        }
    }
}