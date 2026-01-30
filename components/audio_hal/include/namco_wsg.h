/*
 * namco_wsg.h - Namco WSG (Waveform Sound Generator) Emulation
 *
 * 3-channel wavetable synthesis as used in Pac-Man, Galaga, etc.
 */

#ifndef NAMCO_WSG_H
#define NAMCO_WSG_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Namco WSG has 3 sound channels
#define WSG_CHANNELS    3

// Each waveform is 32 samples (4-bit signed)
#define WSG_WAVE_SIZE   32

// Number of different waveforms in Pac-Man
#define WSG_WAVE_COUNT  16

/**
 * Initialize the WSG emulator
 * @param wavetable Pointer to converted wavetable data (from ROM)
 */
void wsg_init(const int8_t *wavetable);

/**
 * Parse sound registers and update internal state
 * @param regs Pointer to 32-byte sound register array
 *
 * Register layout (Pac-Man):
 *   0x00-0x04: Voice 1 frequency (20-bit)
 *   0x05:      Voice 1 waveform select
 *   ...
 *   0x0A-0x0E: Voice 2 frequency
 *   0x0F:      Voice 2 waveform select
 *   ...
 *   0x10:      Voice 0 frequency (low 4 bits only)
 *   0x11-0x14: Voice 0 frequency (remaining)
 *   0x15:      Voice 0 volume
 *   0x16:      Voice 1 volume
 *   ...etc
 */
void wsg_parse_registers(const uint8_t *regs);

/**
 * Render audio samples
 * @param buffer Output buffer (16-bit unsigned PCM, 0x8000 = center)
 * @param samples Number of samples to render
 */
void wsg_render(uint16_t *buffer, uint32_t samples);

#ifdef __cplusplus
}
#endif

#endif // NAMCO_WSG_H
