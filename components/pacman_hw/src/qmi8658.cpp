/*
 * qmi8658.cpp - QMI8658 IMU Driver for PELLETINO
 *
 * Provides tilt-based control using the onboard 6-axis IMU
 * Uses simple gravity vector detection - no calibration needed
 */

#include "qmi8658.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <cmath>
#include <algorithm>

static const char *TAG = "QMI8658";

// I2C address
#define QMI8658_ADDR        0x6B

// Register addresses
#define REG_WHO_AM_I        0x00
#define REG_CTRL1           0x02
#define REG_CTRL2           0x03
#define REG_CTRL3           0x04
#define REG_CTRL5           0x06
#define REG_CTRL7           0x08
#define REG_CTRL8           0x09
#define REG_CTRL9           0x0A
#define REG_STATUS0         0x2E
#define REG_STATUS1         0x2F
#define REG_ACCEL_X_L       0x35

// Expected WHO_AM_I value
#define WHO_AM_I_VALUE      0x05

// Module state
static bool imu_initialized = false;

// Simple moving average (last 4 samples)
static int16_t hist_x[4] = {0};
static int16_t hist_y[4] = {0};
static int hist_idx = 0;

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

    // Reset sequence per QMI8658 datasheet
    // CTRL1[7]: SerialInterface_disable=0, CTRL1[6]:address_ai=1 (auto-increment)
    qmi8658_write_reg(REG_CTRL1, 0x40);

    // CTRL7: Disable all sensors first
    qmi8658_write_reg(REG_CTRL7, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // CTRL2: Accelerometer ODR and Scale
    // Bits 7: self-test=0
    // Bits 6:4: aFS = 000 (±2g)  
    // Bits 3:0: aODR = 0101 (470Hz for more responsive updates)
    qmi8658_write_reg(REG_CTRL2, 0x05);  // ±2g, 250Hz

    // CTRL3: Gyroscope (configure even if not used)
    qmi8658_write_reg(REG_CTRL3, 0x25);

    // CTRL5: LPF disabled for accel and gyro
    qmi8658_write_reg(REG_CTRL5, 0x00);

    // CTRL7: Enable accelerometer
    // Bit 0: aEN = 1
    qmi8658_write_reg(REG_CTRL7, 0x01);

    vTaskDelay(pdMS_TO_TICKS(30));  // Wait for first sample

    // Check status
    uint8_t status = 0;
    qmi8658_read_reg(REG_STATUS1, &status);
    ESP_LOGI(TAG, "STATUS1=0x%02X after enable", status);

    // Do a test read
    int16_t tx, ty, tz;
    qmi8658_read_accel(&tx, &ty, &tz);
    ESP_LOGI(TAG, "Test read: x=%d y=%d z=%d", tx, ty, tz);

    imu_initialized = true;
    ESP_LOGI(TAG, "QMI8658 initialized (±2g scale, 470Hz)");

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

    // Check if new data is available (STATUS1 bit 0)
    uint8_t status = 0;
    qmi8658_read_reg(REG_STATUS1, &status);
    
    // Read regardless - the read itself should trigger new conversion
    uint8_t buf[6];
    if (qmi8658_read_regs(REG_ACCEL_X_L, buf, 6) != ESP_OK) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (z) *z = 0;
        return;
    }

    if (x) *x = (int16_t)((buf[1] << 8) | buf[0]);
    if (y) *y = (int16_t)((buf[3] << 8) | buf[2]);
    if (z) *z = (int16_t)((buf[5] << 8) | buf[4]);
}

void qmi8658_calibrate(void)
{
    // No calibration needed - we use relative tilt from gravity
    ESP_LOGI(TAG, "IMU ready (no calibration needed)");
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

    // Debug: log raw accel values periodically
    static int raw_debug = 0;
    if (++raw_debug >= 120) {
        ESP_LOGI(TAG, "Raw accel: x=%d y=%d z=%d", ax, ay, az);
        raw_debug = 0;
    }

    // Store in history buffer
    hist_x[hist_idx] = ax;
    hist_y[hist_idx] = ay;
    hist_idx = (hist_idx + 1) & 3;

    // Simple average of last 4 samples
    int32_t avg_x = (hist_x[0] + hist_x[1] + hist_x[2] + hist_x[3]) / 4;
    int32_t avg_y = (hist_y[0] + hist_y[1] + hist_y[2] + hist_y[3]) / 4;

    // At ±2g scale: 16384 counts = 1g
    // For 15° tilt: sin(15°) ≈ 0.26 → ~4250 counts
    // Scale to -128..127: divide by 64 gives ~±256 at 1g, ~±66 at 15°
    // Use /128 for more range: ~±128 at 1g, ~±33 at 15°
    int32_t pitch_val = avg_x / 128;
    int32_t roll_val = avg_y / 128;

    *pitch = (int8_t)std::max(-128, std::min(127, (int)pitch_val));
    *roll = (int8_t)std::max(-128, std::min(127, (int)roll_val));
}

void qmi8658_enable_wake_on_motion(void)
{
    if (!imu_initialized) {
        ESP_LOGW(TAG, "IMU not initialized, cannot enable wake-on-motion");
        return;
    }

    // Configure motion detection on QMI8658
    // CTRL8: Motion detection interrupt settings
    // Bit 7: Motion interrupt on INT1
    // Bits 6-0: Motion detection threshold (default works for most cases)
    qmi8658_write_reg(REG_CTRL8, 0x80);  // Enable motion interrupt on INT1

    // Note: On FIESTA26 hardware, QMI8658 INT pin is not connected to ESP32 GPIO
    // Wake-on-motion will be implemented via timer-based polling in deep sleep
    
    ESP_LOGI(TAG, "Motion detection configured (timer-based polling for wake)");
}
