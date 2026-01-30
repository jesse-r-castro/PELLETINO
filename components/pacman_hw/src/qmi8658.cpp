/*
 * qmi8658.cpp - QMI8658 IMU Driver for PELLETINO
 *
 * Provides tilt-based control using the onboard 6-axis IMU
 */

#include "qmi8658.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "QMI8658";

// I2C address
#define QMI8658_ADDR        0x6B

// Register addresses
#define REG_WHO_AM_I        0x00
#define REG_CTRL1           0x02
#define REG_CTRL2           0x03
#define REG_CTRL3           0x04
#define REG_CTRL7           0x08
#define REG_ACCEL_X_L       0x35
#define REG_GYRO_X_L        0x3B

// Expected WHO_AM_I value
#define WHO_AM_I_VALUE      0x05

// Module state
static bool imu_initialized = false;
static int16_t offset_x = 0;
static int16_t offset_y = 0;

// Smoothing state
static int32_t smooth_x = 0;
static int32_t smooth_y = 0;

static esp_err_t qmi8658_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(I2C_NUM_0, QMI8658_ADDR, 
                                         &reg, 1, value, 1, 
                                         pdMS_TO_TICKS(100));
}

static esp_err_t qmi8658_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0, QMI8658_ADDR, 
                                       data, 2, pdMS_TO_TICKS(100));
}

static esp_err_t qmi8658_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_write_read_device(I2C_NUM_0, QMI8658_ADDR,
                                         &reg, 1, buf, len,
                                         pdMS_TO_TICKS(100));
}

bool qmi8658_init(void)
{
    if (imu_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing QMI8658 IMU");

    // Verify WHO_AM_I register
    uint8_t who_am_i = 0;
    esp_err_t ret = qmi8658_read_reg(REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I: %s", esp_err_to_name(ret));
        return false;
    }

    if (who_am_i != WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: got 0x%02X, expected 0x%02X", 
                 who_am_i, WHO_AM_I_VALUE);
        return false;
    }

    ESP_LOGI(TAG, "QMI8658 detected (WHO_AM_I=0x%02X)", who_am_i);

    // CTRL1: Enable sensors, sensor mode
    qmi8658_write_reg(REG_CTRL1, 0x60);

    // CTRL2: Accelerometer configuration
    //   Bits 6:4: Scale = 001 (±4g)
    //   Bits 3:0: ODR = 0100 (235Hz)
    qmi8658_write_reg(REG_CTRL2, 0x14);

    // CTRL3: Gyroscope configuration (not used but configure anyway)
    //   Bits 6:4: Scale = 011 (±512dps)
    //   Bits 3:0: ODR = 0100 (235Hz)
    qmi8658_write_reg(REG_CTRL3, 0x34);

    // CTRL7: Enable sensors
    //   Bit 0: aEN = 1 (accel enabled)
    //   Bit 1: gEN = 1 (gyro enabled)
    qmi8658_write_reg(REG_CTRL7, 0x03);

    imu_initialized = true;
    ESP_LOGI(TAG, "QMI8658 initialized");

    return true;
}

bool qmi8658_is_initialized(void)
{
    return imu_initialized;
}

void qmi8658_read_accel(int16_t *x, int16_t *y, int16_t *z)
{
    if (!imu_initialized) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (z) *z = 0;
        return;
    }

    uint8_t buf[6];
    if (qmi8658_read_regs(REG_ACCEL_X_L, buf, 6) != ESP_OK) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (z) *z = 0;
        return;
    }

    // Little-endian: low byte first
    if (x) *x = (int16_t)((buf[1] << 8) | buf[0]);
    if (y) *y = (int16_t)((buf[3] << 8) | buf[2]);
    if (z) *z = (int16_t)((buf[5] << 8) | buf[4]);
}

void qmi8658_calibrate(void)
{
    if (!imu_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Calibrating IMU (hold device flat)...");

    // Average multiple readings for calibration
    int32_t sum_x = 0, sum_y = 0, sum_z = 0;
    const int samples = 32;

    for (int i = 0; i < samples; i++) {
        int16_t ax, ay, az;
        qmi8658_read_accel(&ax, &ay, &az);
        sum_x += ax;
        sum_y += ay;
        sum_z += az;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    offset_x = sum_x / samples;
    offset_y = sum_y / samples;
    // Don't calibrate Z (should be ~1g when flat)

    // Initialize smoothing state
    smooth_x = 0;
    smooth_y = 0;

    ESP_LOGI(TAG, "Calibration done: offset_x=%d, offset_y=%d", offset_x, offset_y);
}

void qmi8658_get_tilt(int8_t *pitch, int8_t *roll)
{
    if (!pitch || !roll) {
        return;
    }

    if (!imu_initialized) {
        *pitch = 0;
        *roll = 0;
        return;
    }

    int16_t ax, ay, az;
    qmi8658_read_accel(&ax, &ay, &az);

    // Apply calibration offsets
    int32_t cx = (int32_t)ax - offset_x;
    int32_t cy = (int32_t)ay - offset_y;

    // Apply stronger smoothing (IIR filter: 7/8 old + 1/8 new)
    smooth_x = ((smooth_x * 7) + cx) / 8;
    smooth_y = ((smooth_y * 7) + cy) / 8;

    // Convert to normalized range (-128 to 127)
    // At ±4g scale, 8192 counts = 1g
    // For ~15° tilt to trigger: sin(15°) ≈ 0.26g ≈ 2100 counts
    // Scale: divide by ~16 to get reasonable range
    int32_t pitch_val = smooth_x / 16;
    int32_t roll_val = smooth_y / 16;

    // Clamp to valid range
    *pitch = (int8_t)std::max(-128, std::min(127, (int)pitch_val));
    *roll = (int8_t)std::max(-128, std::min(127, (int)roll_val));
}
