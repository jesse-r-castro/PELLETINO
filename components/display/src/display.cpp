/*
 * display.cpp - ST7789 Display Driver for FIESTA26
 */

#include "display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "DISPLAY";

static spi_device_handle_t spi_handle;
static uint8_t *dma_buffer[2] = {nullptr, nullptr};  // Double buffer
static int current_buffer = 0;
static spi_transaction_t trans[2];  // Transaction descriptors
static bool trans_pending = false;
static constexpr size_t DMA_BUFFER_SIZE = GAME_WIDTH * 16 * 2;  // 16 rows for video (was 8)

// ST7789 Commands
#define ST7789_NOP       0x00
#define ST7789_SWRESET   0x01
#define ST7789_SLPOUT    0x11
#define ST7789_NORON     0x13
#define ST7789_INVON     0x21
#define ST7789_DISPON    0x29
#define ST7789_CASET     0x2A
#define ST7789_RASET     0x2B
#define ST7789_RAMWR     0x2C
#define ST7789_MADCTL    0x36
#define ST7789_COLMOD    0x3A

// Pre/post transfer callbacks for DC pin control
static void lcd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_LCD_DC, dc);
}

static void send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void*)0;  // DC = 0 for command
    spi_device_polling_transmit(spi_handle, &t);
}

static void send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void*)1;  // DC = 1 for data
    spi_device_polling_transmit(spi_handle, &t);
}

static void send_data_dma(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void*)1;
    spi_device_transmit(spi_handle, &t);  // Blocking DMA transfer
}

// ST7789 240x280 panels have a Y offset (panel is 320 tall, we use middle 280)
#define ST7789_Y_OFFSET  20

void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint8_t data[4];

    // Column address set
    send_cmd(ST7789_CASET);
    data[0] = (x >> 8) & 0xFF;
    data[1] = x & 0xFF;
    data[2] = ((x + w - 1) >> 8) & 0xFF;
    data[3] = (x + w - 1) & 0xFF;
    send_data(data, 4);

    // Row address set - add Y offset for 240x280 panel
    uint16_t y_adj = y + ST7789_Y_OFFSET;
    uint16_t y_end = y + h - 1 + ST7789_Y_OFFSET;
    send_cmd(ST7789_RASET);
    data[0] = (y_adj >> 8) & 0xFF;
    data[1] = y_adj & 0xFF;
    data[2] = (y_end >> 8) & 0xFF;
    data[3] = y_end & 0xFF;
    send_data(data, 4);

    // Memory write
    send_cmd(ST7789_RAMWR);
}

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7789 display (240x280)");

    // Configure GPIO pins
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Hardware reset
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Initialize SPI bus
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_LCD_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = PIN_LCD_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = DMA_BUFFER_SIZE;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Attach LCD device
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = LCD_SPI_CLOCK;
    devcfg.mode = 0;
    devcfg.spics_io_num = PIN_LCD_CS;
    devcfg.queue_size = 7;
    devcfg.pre_cb = lcd_spi_pre_transfer_callback;
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &devcfg, &spi_handle));

    // Allocate double DMA buffers for async operation
    dma_buffer[0] = (uint8_t*)heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA);
    dma_buffer[1] = (uint8_t*)heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA);
    if (!dma_buffer[0] || !dma_buffer[1]) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffers!");
        return;
    }
    memset(&trans[0], 0, sizeof(spi_transaction_t));
    memset(&trans[1], 0, sizeof(spi_transaction_t));

    // ST7789 initialization sequence
    send_cmd(ST7789_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    send_cmd(ST7789_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Pixel format: 16-bit RGB565
    send_cmd(ST7789_COLMOD);
    uint8_t colmod = 0x55;  // 16-bit
    send_data(&colmod, 1);

    // Memory access control (rotation/mirroring)
    send_cmd(ST7789_MADCTL);
    uint8_t madctl = 0x00;  // Adjust as needed for orientation
    send_data(&madctl, 1);

    // Inversion on (ST7789 typically needs this)
    send_cmd(ST7789_INVON);

    // Normal display mode
    send_cmd(ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display on
    send_cmd(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Set backlight
    display_set_backlight(255);

    // Clear screen
    display_fill(0x0000);

    ESP_LOGI(TAG, "Display initialized");
}

void display_write(const uint16_t *data, uint32_t len)
{
    size_t bytes = len * 2;
    if (bytes > DMA_BUFFER_SIZE) {
        bytes = DMA_BUFFER_SIZE;
    }

    // Wait for previous transfer to complete
    if (trans_pending) {
        spi_transaction_t *rtrans;
        spi_device_get_trans_result(spi_handle, &rtrans, portMAX_DELAY);
        trans_pending = false;
    }

    // Copy to current DMA buffer with byte swap
    uint8_t *dst = dma_buffer[current_buffer];
    const uint8_t *src = (const uint8_t*)data;
    for (size_t i = 0; i < bytes; i += 2) {
        dst[i] = src[i + 1];
        dst[i + 1] = src[i];
    }

    // Start async transfer
    trans[current_buffer].length = bytes * 8;
    trans[current_buffer].tx_buffer = dst;
    trans[current_buffer].user = (void*)1;  // DC = 1 for data
    spi_device_queue_trans(spi_handle, &trans[current_buffer], portMAX_DELAY);
    trans_pending = true;

    // Swap buffers
    current_buffer = 1 - current_buffer;
}

void display_write_preswapped(const uint16_t *data, uint32_t len)
{
    // For pre-byte-swapped data - no copy needed, just DMA directly
    size_t bytes = len * 2;
    if (bytes > DMA_BUFFER_SIZE) {
        bytes = DMA_BUFFER_SIZE;
    }

    // Wait for previous transfer
    if (trans_pending) {
        spi_transaction_t *rtrans;
        spi_device_get_trans_result(spi_handle, &rtrans, portMAX_DELAY);
        trans_pending = false;
    }

    // Copy to DMA buffer (data already byte-swapped)
    memcpy(dma_buffer[current_buffer], data, bytes);

    // Start async transfer
    trans[current_buffer].length = bytes * 8;
    trans[current_buffer].tx_buffer = dma_buffer[current_buffer];
    trans[current_buffer].user = (void*)1;
    spi_device_queue_trans(spi_handle, &trans[current_buffer], portMAX_DELAY);
    trans_pending = true;

    current_buffer = 1 - current_buffer;
}

void display_wait_done(void)
{
    if (trans_pending) {
        spi_transaction_t *rtrans;
        spi_device_get_trans_result(spi_handle, &rtrans, portMAX_DELAY);
        trans_pending = false;
    }
}

void display_fill(uint16_t color)
{
    // Byte-swap color
    uint16_t swapped = ((color >> 8) | (color << 8));

    display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Fill in chunks using buffer 0
    size_t chunk_pixels = DMA_BUFFER_SIZE / 2;
    uint16_t *buf = (uint16_t*)dma_buffer[0];
    for (size_t i = 0; i < chunk_pixels; i++) {
        buf[i] = swapped;
    }

    size_t total_pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    while (total_pixels > 0) {
        size_t pixels = (total_pixels > chunk_pixels) ? chunk_pixels : total_pixels;
        send_data_dma(dma_buffer[0], pixels * 2);
        total_pixels -= pixels;
    }
}

void display_set_backlight(uint8_t brightness)
{
    // Configure LEDC for PWM backlight control
    static bool ledc_initialized = false;

    if (!ledc_initialized) {
        ledc_timer_config_t timer_conf = {};
        timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_conf.timer_num = LEDC_TIMER_0;
        timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
        timer_conf.freq_hz = 5000;
        timer_conf.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&timer_conf);

        ledc_channel_config_t channel_conf = {};
        channel_conf.speed_mode = LEDC_LOW_SPEED_MODE;
        channel_conf.channel = LEDC_CHANNEL_0;
        channel_conf.timer_sel = LEDC_TIMER_0;
        channel_conf.intr_type = LEDC_INTR_DISABLE;
        channel_conf.gpio_num = PIN_LCD_BL;
        channel_conf.duty = 0;
        channel_conf.hpoint = 0;
        ledc_channel_config(&channel_conf);

        ledc_initialized = true;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
