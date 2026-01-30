/*
 * display.h - ST7789 Display Driver for FIESTA26
 *
 * 240x280 resolution, SPI interface with DMA
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Display dimensions
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280

// Game native dimensions (Pac-Man)
#define GAME_WIDTH      224
#define GAME_HEIGHT     288

// Centering offset (game is centered horizontally, slight crop vertically)
#define GAME_X_OFFSET   ((DISPLAY_WIDTH - GAME_WIDTH) / 2)   // 8 pixels
#define GAME_Y_OFFSET   (-4)  // Crop 4 pixels top and bottom

// GPIO Pin definitions (FIESTA26)
#define PIN_LCD_MOSI    GPIO_NUM_2
#define PIN_LCD_SCLK    GPIO_NUM_1
#define PIN_LCD_CS      GPIO_NUM_5
#define PIN_LCD_DC      GPIO_NUM_3
#define PIN_LCD_RST     GPIO_NUM_4
#define PIN_LCD_BL      GPIO_NUM_6

// SPI configuration
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_CLOCK   80000000  // 80 MHz (max for ST7789)

/**
 * Initialize the display driver
 */
void display_init(void);

/**
 * Write pixel data to display
 * @param data Pointer to RGB565 pixel data
 * @param len Number of pixels (not bytes)
 */
void display_write(const uint16_t *data, uint32_t len);

/**
 * Write pre-byte-swapped pixel data (faster, no copy needed)
 * @param data Pointer to pre-swapped RGB565 pixel data
 * @param len Number of pixels (not bytes)
 */
void display_write_preswapped(const uint16_t *data, uint32_t len);

/**
 * Wait for pending DMA transfer to complete
 */
void display_wait_done(void);

/**
 * Set the drawing window
 * @param x X coordinate (0-239)
 * @param y Y coordinate (0-279)
 * @param w Width
 * @param h Height
 */
void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * Fill screen with a solid color
 * @param color RGB565 color
 */
void display_fill(uint16_t color);

/**
 * Set backlight brightness
 * @param brightness 0-255
 */
void display_set_backlight(uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
