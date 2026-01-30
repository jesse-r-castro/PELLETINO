/*
 * qmi8658.h - QMI8658 IMU Driver for PELLETINO
 *
 * Provides tilt-based control using the onboard 6-axis IMU
 */

#ifndef QMI8658_H
#define QMI8658_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the QMI8658 IMU
 * @return true if successful
 */
bool qmi8658_init(void);

/**
 * Check if IMU is initialized
 * @return true if ready
 */
bool qmi8658_is_initialized(void);

/**
 * Read raw accelerometer values
 * @param x Pointer to store X acceleration (can be NULL)
 * @param y Pointer to store Y acceleration (can be NULL)
 * @param z Pointer to store Z acceleration (can be NULL)
 */
void qmi8658_read_accel(int16_t *x, int16_t *y, int16_t *z);

/**
 * Calibrate the IMU (call when device is held flat)
 */
void qmi8658_calibrate(void);

/**
 * Get normalized tilt values for game input
 * @param pitch Output pitch (-128 to 127, forward/backward tilt)
 * @param roll Output roll (-128 to 127, left/right tilt)
 */
void qmi8658_get_tilt(int8_t *pitch, int8_t *roll);

#ifdef __cplusplus
}
#endif

#endif // QMI8658_H
