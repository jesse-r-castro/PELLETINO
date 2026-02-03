/*
 * audio_hal.h - ES8311 Audio HAL for FIESTA26
 *
 * Provides I2S audio output through ES8311 codec
 */

#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Audio configuration
#define AUDIO_SAMPLE_RATE   44100   // 44.1 kHz (CD quality)
#define AUDIO_BUFFER_SIZE   64      // Samples per buffer (smaller = lower latency)
#define AUDIO_DMA_BUFFERS   8       // Number of DMA buffers (more = less underruns)

// I2S Pin definitions (FIESTA26)
#define PIN_I2S_MCK     GPIO_NUM_19
#define PIN_I2S_BCK     GPIO_NUM_20
#define PIN_I2S_LRCK    GPIO_NUM_22
#define PIN_I2S_DOUT    GPIO_NUM_23
#define PIN_I2S_DIN     GPIO_NUM_21

// I2C for ES8311 control (shared bus)
#define PIN_I2C_SDA     GPIO_NUM_8
#define PIN_I2C_SCL     GPIO_NUM_7
#define ES8311_ADDR     0x18

/**
 * Initialize audio subsystem (ES8311 + I2S)
 */
void audio_init(void);

/**
 * Update audio - call every frame to refill buffers
 * This reads from Namco WSG sound registers and generates samples
 */
void audio_update(void);

/**
 * Transmit any pending audio buffers
 * Non-blocking - uses DMA
 */
void audio_transmit(void);

/**
 * Set master volume
 * @param volume 0-255
 */
void audio_set_volume(uint8_t volume);

/**
 * Get pointer to sound registers (for Z80 memory mapping)
 * @return Pointer to 32-byte sound register array
 */
uint8_t* audio_get_sound_registers(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_HAL_H
