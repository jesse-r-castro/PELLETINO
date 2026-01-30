/*
 * pacman_video.cpp - Pac-Man Video Rendering
 *
 * Tile and sprite rendering ported from Galagino
 */

#include "pacman_video.h"
#include "display.h"
#include "audio_hal.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include <cstring>
#include <cstdlib>

static const char *TAG = "PACMAN_VIDEO";

// Graphics data pointers
static const uint16_t *tile_gfx = nullptr;     // 8x8 tiles, 2bpp packed
static const uint32_t *sprite_gfx = nullptr;   // 16x16 sprites, 2bpp packed
static const uint16_t *colormap_orig = nullptr;// Original RGB565 color lookup
static uint16_t *colormap = nullptr;           // Byte-swapped for direct DMA

// Frame buffer for one tile row (8 scanlines × 224 pixels × 2 bytes)
static uint16_t *frame_buffer = nullptr;

// Sprite state for current frame
struct Sprite {
    int16_t x, y;
    uint8_t code;
    uint8_t color;
    uint8_t flags;  // bit 0: flip X, bit 1: flip Y
};
static Sprite active_sprites[MAX_SPRITES];
static uint8_t num_active_sprites = 0;

// Tile address lookup table (matches Galagino's tileaddr.h)
// Maps screen position to VRAM address
static uint16_t tileaddr[TILES_Y][TILES_X];

static void init_tileaddr_table(void)
{
    /*
     * Pac-Man's video RAM layout is non-linear (rotated screen)
     * This generates the same mapping as Galagino's tileaddr.py
     */
    for (int row = 0; row < TILES_Y; row++) {
        for (int col = 0; col < TILES_X; col++) {
            uint16_t addr;

            if (row < 2) {
                // Top 2 rows (score area)
                addr = 0x3DD + col - 32 * row;
            } else if (row >= 34) {
                // Bottom 2 rows
                addr = 0x01D + col - 32 * (row - 34);
            } else {
                // Main playfield (rows 2-33)
                addr = 0x3A0 + (row - 2) - 32 * col;
            }

            tileaddr[row][col] = addr;
        }
    }
}

void pacman_video_init(void)
{
    ESP_LOGI(TAG, "Initializing Pac-Man video");

    // Allocate frame buffer for one tile row
    frame_buffer = (uint16_t*)heap_caps_malloc(GAME_WIDTH * TILE_HEIGHT * sizeof(uint16_t),
                                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer!");
        return;
    }

    // Initialize tile address lookup
    init_tileaddr_table();

    ESP_LOGI(TAG, "Video initialized");
}

void pacman_video_set_tiles(const uint16_t *tiles)
{
    tile_gfx = tiles;
}

void pacman_video_set_sprites(const uint32_t *sprites)
{
    sprite_gfx = sprites;
}

void pacman_video_set_palette(const uint16_t *palette)
{
    colormap_orig = palette;
    
    // Allocate and pre-byte-swap colormap for direct DMA (64 colors * 4 shades)
    if (!colormap) {
        colormap = (uint16_t*)heap_caps_malloc(64 * 4 * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    }
    if (colormap && palette) {
        for (int i = 0; i < 64 * 4; i++) {
            uint16_t c = palette[i];
            colormap[i] = (c >> 8) | (c << 8);  // Byte-swap for ST7789
        }
        ESP_LOGI(TAG, "Palette byte-swapped for DMA");
    }
}

// Prepare sprite list for current frame
static void prepare_sprites(const uint8_t *memory)
{
    num_active_sprites = 0;

    for (int idx = 0; idx < 8 && num_active_sprites < MAX_SPRITES; idx++) {
        // Sprite attributes are at memory offsets 0x0FF0 (from 0x4FF0 in Z80 space)
        const uint8_t *sprite_base = memory + 2 * (7 - idx);

        Sprite spr;
        spr.code = sprite_base[0x0FF0] >> 2;
        spr.color = sprite_base[0x0FF1] & 63;
        spr.flags = sprite_base[0x0FF0] & 3;

        // Position from 0x5060 area (mapped to memory + 0x1060)
        spr.x = 255 - 16 - memory[0x1060 + 2 * (7 - idx)];
        spr.y = 16 + 256 - memory[0x1061 + 2 * (7 - idx)];

        // Only add visible sprites
        if ((spr.code < 64) &&
            (spr.y > -16) && (spr.y < GAME_HEIGHT) &&
            (spr.x > -16) && (spr.x < GAME_WIDTH)) {
            active_sprites[num_active_sprites++] = spr;
        }
    }
}

// Render a single 8x8 tile into frame buffer - optimized
static void IRAM_ATTR blit_tile(int row, int col, const uint8_t *memory)
{
    uint16_t addr = tileaddr[row][col];
    uint8_t tile_idx = memory[addr];
    uint8_t color_idx = memory[0x400 + addr] & 63;

    // Get tile graphics (8 words, one per row, packed 2bpp)
    const uint16_t *tile = &tile_gfx[tile_idx * 8];
    const uint16_t *colors = &colormap[color_idx * 4];

    uint16_t *ptr = frame_buffer + col * TILE_WIDTH;

    // 8 pixel rows per tile
    for (int r = 0; r < TILE_HEIGHT; r++) {
        uint16_t pix = tile[r];
        
        // Write 8 pixels using direct array indexing (color 0 = transparent, skip)
        uint8_t p;
        p = pix & 3;        if (p) ptr[0] = colors[p];
        p = (pix >> 2) & 3; if (p) ptr[1] = colors[p];
        p = (pix >> 4) & 3; if (p) ptr[2] = colors[p];
        p = (pix >> 6) & 3; if (p) ptr[3] = colors[p];
        p = (pix >> 8) & 3; if (p) ptr[4] = colors[p];
        p = (pix >> 10) & 3; if (p) ptr[5] = colors[p];
        p = (pix >> 12) & 3; if (p) ptr[6] = colors[p];
        p = (pix >> 14) & 3; if (p) ptr[7] = colors[p];
        
        ptr += GAME_WIDTH;
    }
}

// Render a 16x16 sprite into frame buffer
static void blit_sprite(int row, const Sprite &spr)
{
    if (!sprite_gfx || !colormap) return;

    // Get sprite graphics with flip handling
    const uint32_t *sprite = &sprite_gfx[(spr.flags & 3) * 64 * 16 + spr.code * 16];
    const uint16_t *colors = &colormap[(spr.color & 63) * 4];

    // Create mask for sprites that clip left or right
    uint32_t mask = 0xFFFFFFFF;
    if (spr.x < 0) mask <<= -2 * spr.x;
    if (spr.x > GAME_WIDTH - 16) mask >>= 2 * (spr.x - (GAME_WIDTH - 16));

    int16_t y_offset = spr.y - 8 * row;

    // Check how many lines to draw in this row
    int lines_to_draw = 8;
    if (y_offset < -8) lines_to_draw = 16 + y_offset;

    // Check which sprite line to start with
    int startline = 0;
    if (y_offset > 0) {
        startline = y_offset;
        lines_to_draw = 8 - y_offset;
    }

    // Skip into sprite image if needed
    if (y_offset < 0) {
        sprite -= y_offset;
    }

    // Calculate pixel destination
    uint16_t *ptr = frame_buffer + spr.x + GAME_WIDTH * startline;

    // Render sprite lines
    for (int r = 0; r < lines_to_draw; r++, ptr += (GAME_WIDTH - 16)) {
        uint32_t pix = *sprite++ & mask;

        // 16 pixel columns per sprite
        for (int c = 0; c < 16; c++, pix >>= 2) {
            uint8_t px = pix & 3;
            if (px && spr.x + c >= 0 && spr.x + c < GAME_WIDTH) {
                uint16_t color = colors[px];
                if (color) *ptr = color;
            }
            ptr++;
        }
    }
}

// Render one tile row (8 scanlines)
static void IRAM_ATTR render_tile_row(int row, const uint8_t *memory)
{
    // Clear row buffer to black
    memset(frame_buffer, 0, GAME_WIDTH * TILE_HEIGHT * sizeof(uint16_t));

    // Render 28 tile columns
    for (int col = 0; col < TILES_X; col++) {
        blit_tile(row, col, memory);
    }

    // Render sprites that overlap this row
    for (int s = 0; s < num_active_sprites; s++) {
        const Sprite &spr = active_sprites[s];

        // Check if sprite is visible on this row
        if ((spr.y < 8 * (row + 1)) && ((spr.y + 16) > 8 * row)) {
            blit_sprite(row, spr);
        }
    }
}

void pacman_video_render_frame(const uint8_t *memory)
{
    if (!frame_buffer || !memory) return;

    // Prepare sprite list
    prepare_sprites(memory);

    // Set display window - game is 224x288, display is 240x280
    // X: center with 8 pixel padding on each side
    // Y: render full height, bottom 8 pixels will be clipped by display
    display_set_window(GAME_X_OFFSET, 0, GAME_WIDTH, DISPLAY_HEIGHT);

    // Render and transmit each tile row (36 rows × 8 pixels = 288)
    // We render 35 rows (280 pixels) to fill display exactly
    for (int row = 0; row < 35; row++) {
        render_tile_row(row, memory);

        // Write tile row using pre-swapped colors for faster DMA
        display_write_preswapped(frame_buffer, GAME_WIDTH * TILE_HEIGHT);

        // Update audio every 12 rows (larger buffer = less frequent updates needed)
        if (row % 12 == 0) {
            audio_update();
        }
    }
    
    // Wait for final DMA transfer to complete
    display_wait_done();
}
