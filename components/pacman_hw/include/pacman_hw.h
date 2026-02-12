/*
 * pacman_hw.h - Pac-Man Hardware Emulation
 *
 * Memory map, I/O, and main emulation control
 */

#ifndef PACMAN_HW_H
#define PACMAN_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Memory map sizes
#define PACMAN_ROM_SIZE     0x4000  // 16KB program ROM
#define PACMAN_VRAM_SIZE    0x0400  // 1KB video RAM
#define PACMAN_CRAM_SIZE    0x0400  // 1KB color RAM
#define PACMAN_RAM_SIZE     0x0800  // 2KB work RAM
#define PACMAN_SPRITE_SIZE  0x0010  // 16 bytes sprite RAM

// Z80 cycles per frame (3.072 MHz / 60 Hz)
#define PACMAN_CYCLES_PER_FRAME  51200

// DIP switch settings
#define PACMAN_DIP_DEFAULT  0xC9  // 3 lives, bonus at 10K, cocktail

/**
 * Initialize Pac-Man hardware emulation
 */
void pacman_hw_init(void);

/**
 * Reset Pac-Man hardware
 */
void pacman_hw_reset(void);

/**
 * Run one frame of emulation
 */
void pacman_run_frame(void);

/**
 * Render the current frame to display
 */
void pacman_render_screen(void);

/**
 * Poll input devices and update input state
 */
void pacman_poll_input(void);

/**
 * Trigger VBLANK interrupt if enabled
 */
void pacman_vblank_interrupt(void);

/**
 * Set ROM data pointer (for XIP or RAM-loaded ROM)
 * @param rom Pointer to 16KB ROM data
 */
void pacman_set_rom(const uint8_t *rom);

/**
 * Set tile graphics data
 * @param tiles Pointer to converted tile data
 */
void pacman_set_tiles(const uint16_t *tiles);

/**
 * Set sprite graphics data
 * @param sprites Pointer to converted sprite data
 */
void pacman_set_sprites(const uint32_t *sprites);

/**
 * Set color palette data
 * @param palette Pointer to RGB565 color palette
 */
void pacman_set_palette(const uint16_t *palette);

/**
 * Set audio wavetable data
 * @param wavetable Pointer to converted wavetable
 */
void pacman_set_wavetable(const int8_t *wavetable);

/**
 * Load ROM files and initialize hardware
 * Call this after all set_* functions
 */
void pacman_load_roms(void);

/**
 * Get pointer to Pac-Man memory (for game state inspection)
 * @return Pointer to internal memory array (VRAM/CRAM/RAM)
 */
const uint8_t* pacman_get_memory(void);

/**
 * Get writable pointer to Pac-Man memory (for credit reset, etc.)
 * @return Writable pointer to internal memory array
 */
uint8_t* pacman_get_memory_rw(void);

#ifdef __cplusplus
}
#endif

#endif // PACMAN_HW_H
