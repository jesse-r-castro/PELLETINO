/*
 * audio_hal.cpp - ES8311 Audio HAL Implementation
 */

#include "audio_hal.h"
#include "namco_wsg.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "AUDIO";

// Sound registers (written by Z80, read by audio)
static uint8_t sound_regs[32] = {0};

// Audio sample buffer (unsigned 16-bit for ES8311)
static uint16_t sample_buffer[AUDIO_BUFFER_SIZE];

// I2S handle
static i2s_chan_handle_t i2s_tx_handle = nullptr;

// ES8311 register definitions
#define ES8311_REG_RESET        0x00
#define ES8311_REG_CLK_MANAGER  0x01
#define ES8311_REG_SDPOUT       0x09
#define ES8311_REG_SDPIN        0x0A
#define ES8311_REG_ADC_VOL      0x17
#define ES8311_REG_DAC_VOL      0x32
#define ES8311_REG_SYS_CTRL     0x0D
#define ES8311_REG_GPIO         0x44

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0, ES8311_ADDR, data, 2, pdMS_TO_TICKS(100));
}

static esp_err_t es8311_init(void)
{
    ESP_LOGI(TAG, "Initializing ES8311 codec");

    // Initialize I2C (may already be initialized for other devices)
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = PIN_I2C_SDA;
    i2c_conf.scl_io_num = PIN_I2C_SCL;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = 100000;

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C config failed (may already be configured): %s", esp_err_to_name(ret));
    }

    ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Reset ES8311
    es8311_write_reg(ES8311_REG_RESET, 0x3F);
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(ES8311_REG_RESET, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Clock manager - use internal clock divider
    es8311_write_reg(0x01, 0x3F);  // CLK Manager 1
    es8311_write_reg(0x02, 0x00);  // CLK Manager 2
    es8311_write_reg(0x03, 0x10);  // CLK Manager 3
    es8311_write_reg(0x04, 0x10);  // CLK Manager 4
    es8311_write_reg(0x05, 0x00);  // CLK Manager 5
    es8311_write_reg(0x06, 0x03);  // CLK Manager 6
    es8311_write_reg(0x07, 0x00);  // CLK Manager 7
    es8311_write_reg(0x08, 0xFF);  // CLK Manager 8

    // Serial data port configuration (I2S format)
    es8311_write_reg(ES8311_REG_SDPOUT, 0x00);  // 16-bit I2S
    es8311_write_reg(ES8311_REG_SDPIN, 0x00);

    // System control
    es8311_write_reg(ES8311_REG_SYS_CTRL, 0x00);
    es8311_write_reg(0x0E, 0x02);  // System Control 2
    es8311_write_reg(0x0F, 0x44);  // System Control 3
    es8311_write_reg(0x10, 0x0C);  // System Power
    es8311_write_reg(0x11, 0x00);  // System Power

    // DAC settings
    es8311_write_reg(0x12, 0x00);
    es8311_write_reg(0x13, 0x10);  // ADC/DAC config
    es8311_write_reg(0x14, 0x10);
    es8311_write_reg(ES8311_REG_DAC_VOL, 0xBF);  // DAC volume (fairly loud)

    // ADC settings (not used but configure anyway)
    es8311_write_reg(ES8311_REG_ADC_VOL, 0xBF);

    // Enable DAC
    es8311_write_reg(0x00, 0x80);  // Reset cleared, chip active
    es8311_write_reg(0x01, 0x3F);  // Clocks enabled

    ESP_LOGI(TAG, "ES8311 initialized");
    return ESP_OK;
}

static esp_err_t i2s_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S at %d Hz", AUDIO_SAMPLE_RATE);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_DMA_BUFFERS;
    chan_cfg.dma_frame_num = AUDIO_BUFFER_SIZE;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCK,
            .bclk = PIN_I2S_BCK,
            .ws = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din = PIN_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));

    ESP_LOGI(TAG, "I2S initialized");
    return ESP_OK;
}

void audio_init(void)
{
    ESP_LOGI(TAG, "Initializing audio subsystem");

    // Initialize ES8311 codec
    es8311_init();

    // Initialize I2S
    i2s_init();

    // Initialize Namco WSG (wavetable will be set later from ROM)
    wsg_init(nullptr);

    memset(sound_regs, 0, sizeof(sound_regs));
    memset(sample_buffer, 0, sizeof(sample_buffer));

    ESP_LOGI(TAG, "Audio subsystem initialized");
}

void audio_update(void)
{
    // Parse current sound register state
    wsg_parse_registers(sound_regs);

    // Render samples
    wsg_render(sample_buffer, AUDIO_BUFFER_SIZE);

    // Transmit via I2S
    audio_transmit();
}

void audio_transmit(void)
{
    if (!i2s_tx_handle) return;

    size_t bytes_written = 0;
    // Non-blocking write - if buffer full, skip this update (DMA buffers provide headroom)
    i2s_channel_write(i2s_tx_handle, sample_buffer, sizeof(sample_buffer), &bytes_written, 0);
}

void audio_set_volume(uint8_t volume)
{
    // Map 0-255 to ES8311 volume range
    uint8_t es_vol = volume;  // Direct mapping for now
    es8311_write_reg(ES8311_REG_DAC_VOL, es_vol);
}

uint8_t* audio_get_sound_registers(void)
{
    return sound_regs;
}
