/*
 * pacman_hw.cpp - Pac-Man Hardware Emulation
 *
 * Memory map and I/O implementation ported from Galagino
 */

#include "pacman_hw.h"
#include "pacman_video.h"
#include "pacman_input.h"
#include "z80_cpu.h"
#include "audio_hal.h"
#include "namco_wsg.h"
#include "display.h"
#include "esp_log.h"
#include "esp_attr.h"
#include <cstring>

static const char *TAG = "PACMAN_HW";

// Memory arrays
static uint8_t *memory = nullptr;  // Combined VRAM + CRAM + RAM + Sprite
static const uint8_t *rom_data = nullptr;

// Hardware state
static uint8_t irq_enable = 0;
static uint8_t irq_vector = 0;
static uint8_t game_started = 0;

// ROM/graphics pointers (set by pacman_set_* functions)
static const uint16_t *tile_data = nullptr;
static const uint32_t *sprite_data = nullptr;
static const uint16_t *palette_data = nullptr;

// Memory offsets within the unified memory array
#define MEM_VRAM_OFFSET    0x0000  // 0x4000-0x43FF -> 0x0000
#define MEM_CRAM_OFFSET    0x0400  // 0x4400-0x47FF -> 0x0400
#define MEM_RAM_OFFSET     0x0800  // 0x4800-0x4FFF -> 0x0800
#define MEM_SPRITE_OFFSET  0x0FF0  // Sprite RAM at 0x4FF0
#define MEM_SPRITE2_OFFSET 0x1060  // Sprite RAM 2 at 0x5060

// Sound registers (mapped to audio_hal)
static uint8_t *sound_regs = nullptr;

void pacman_hw_init(void)
{
    ESP_LOGI(TAG, "Initializing Pac-Man hardware");

    // Allocate memory (VRAM + CRAM + RAM + sprite areas)
    // Total: 0x4000-0x5FFF = 8KB but we only need active portions
    memory = (uint8_t*)calloc(0x2000, 1);
    if (!memory) {
        ESP_LOGE(TAG, "Failed to allocate memory!");
        return;
    }

    // Get sound register pointer from audio HAL
    sound_regs = audio_get_sound_registers();

    // Initialize video subsystem
    pacman_video_init();

    // Initialize input
    pacman_input_init();

    ESP_LOGI(TAG, "Pac-Man hardware initialized");
}

void pacman_hw_reset(void)
{
    memset(memory, 0, 0x2000);
    irq_enable = 0;
    irq_vector = 0;
    game_started = 0;

    z80_reset();
}

void pacman_set_rom(const uint8_t *rom)
{
    rom_data = rom;
}

void pacman_set_tiles(const uint16_t *tiles)
{
    tile_data = tiles;
    pacman_video_set_tiles(tiles);
}

void pacman_set_sprites(const uint32_t *sprites)
{
    sprite_data = sprites;
    pacman_video_set_sprites(sprites);
}

void pacman_set_palette(const uint16_t *palette)
{
    palette_data = palette;
    pacman_video_set_palette(palette);
}

void pacman_set_wavetable(const int8_t *wavetable)
{
    // Forward to WSG emulator via audio_hal
    extern void wsg_init(const int8_t *wavetable);
    wsg_init(wavetable);
}

void pacman_load_roms(void)
{
    ESP_LOGI(TAG, "Loading Pac-Man ROMs");

    if (!rom_data) {
        ESP_LOGE(TAG, "ROM data not set!");
        return;
    }

    ESP_LOGI(TAG, "ROM and graphics loaded");
    pacman_hw_reset();
}

// Z80 memory callbacks - extern "C" for linking with z80_cpu.c
extern "C" {

// Z80 memory read callback - IRAM for speed (called millions of times/frame)
IRAM_ATTR uint8_t pacman_mem_read(uint16_t addr)
{
    // 0x0000-0x3FFF: Program ROM (most common case - put first)
    if (addr < 0x4000) {
        return rom_data[addr];
    }

    // 0x4000-0x4FFF: Video RAM, Color RAM, Work RAM
    if (addr < 0x5000) {
        return memory[addr - 0x4000];
    }

    // 0x5000-0x50FF: I/O reads
    if (addr < 0x5100) {
        if (addr == 0x5000) {
            return pacman_read_in0();
        }
        if (addr == 0x5040) {
            return pacman_read_in1();
        }
        if (addr == 0x5080) {
            return PACMAN_DIP_DEFAULT;
        }
        return 0xFF;
    }

    // 0x8000-0x9FFF: Ms. Pac-Man auxiliary ROM (maps to rom_data 0x4000-0x5FFF)
    if (addr >= 0x8000 && addr < 0xA000) {
        return rom_data[addr - 0x4000];  // 0x8000 -> 0x4000, 0x9FFF -> 0x5FFF
    }

    return 0xFF;
}

// Z80 memory write callback - IRAM for speed
IRAM_ATTR void pacman_mem_write(uint16_t addr, uint8_t value)
{
    addr &= 0x7FFF;  // A15 is unused

    // 0x4000-0x4FFF: Video RAM, Color RAM, Work RAM (most common)
    if (addr >= 0x4000 && addr < 0x5000) {
        memory[addr - 0x4000] = value;
        return;
    }

    // 0x5000-0x50FF: I/O writes
    if (addr >= 0x5000 && addr < 0x5100) {
        // 0x5060-0x506F: Sprite RAM 2
        if (addr >= 0x5060 && addr < 0x5070) {
            memory[addr - 0x4000] = value;
            return;
        }

        // 0x5000: Interrupt enable
        if (addr == 0x5000) {
            irq_enable = value & 1;
            return;
        }

        // 0x5040-0x505F: Sound registers
        if (addr >= 0x5040 && addr < 0x5060) {
            uint8_t reg = addr - 0x5040;
            sound_regs[reg] = value & 0x0F;
            return;
        }
    }
}

// Z80 I/O port write (used for interrupt vector)
void pacman_io_write(uint16_t port, uint8_t value)
{
    (void)port;
    irq_vector = value;
}

uint8_t pacman_io_read(uint16_t port)
{
    (void)port;
    return 0xFF;
}

} // extern "C"

void pacman_run_frame(void)
{
    // Execute Z80 for one frame worth of cycles
    z80_execute(PACMAN_CYCLES_PER_FRAME);
}

void pacman_render_screen(void)
{
    pacman_video_render_frame(memory);
}

void pacman_poll_input(void)
{
    pacman_input_update();
}

void pacman_vblank_interrupt(void)
{
    if (irq_enable) {
        z80_interrupt(irq_vector);
    }
}

const uint8_t* pacman_get_memory(void)
{
    return memory;
}

uint8_t* pacman_get_memory_rw(void)
{
    return memory;
}
