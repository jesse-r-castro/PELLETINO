/*
 * fiesta_video.c - MJPEG Video Player for FIESTA26
 *
 * Manually parses MJPEG stream and decodes using TinyJPEG (tjpgd)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tjpgd.h"

// Forward declare display functions
extern void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
extern void display_write_preswapped(const uint16_t *data, uint32_t len);
extern void display_wait_done(void);
extern void display_fill(uint16_t color);

#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280

static const char *TAG = "FIESTA_VIDEO";

// JPEG markers
#define JPEG_SOI  0xFFD8  // Start Of Image
#define JPEG_EOI  0xFFD9  // End Of Image

// Video playback settings
#define TARGET_FPS        24      // 24 frames per second
#define FRAME_TIME_MS     (1000 / TARGET_FPS)

// External video data from movie/fiesta_data.h
#include "../../movie/fiesta_data.h"

// TinyJPEG work buffer (3100 bytes recommended minimum)
#define TJPGD_WORKSPACE_SIZE  4096
static uint8_t tjpgd_work[TJPGD_WORKSPACE_SIZE];

// Line buffer for RGB565 conversion
static uint16_t mcu_line_buffer[DISPLAY_WIDTH];

// Context for JPEG decoder callbacks
typedef struct {
    const uint8_t *jpeg_data;
    size_t jpeg_size;
    size_t jpeg_pos;
} jpeg_decode_ctx_t;

/**
 * TinyJPEG input function - reads data from flash
 */
static unsigned int tjpgd_input_func(JDEC *jd, uint8_t *buff, unsigned int nbyte)
{
    jpeg_decode_ctx_t *ctx = (jpeg_decode_ctx_t *)jd->device;
    
    if (buff == NULL) {
        // Move read pointer
        ctx->jpeg_pos += nbyte;
        return nbyte;
    }
    
    // Check bounds
    if (ctx->jpeg_pos + nbyte > ctx->jpeg_size) {
        nbyte = ctx->jpeg_size - ctx->jpeg_pos;
    }
    
    // Copy from flash to buffer
    memcpy(buff, ctx->jpeg_data + ctx->jpeg_pos, nbyte);
    ctx->jpeg_pos += nbyte;
    
    return nbyte;
}

/**
 * TinyJPEG output function - converts RGB888 to RGB565 and writes scanlines
 * TinyJPEG outputs RGB888 (JD_FORMAT=0), we convert to RGB565
 */
static unsigned int tjpgd_output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    uint8_t *src = (uint8_t *)bitmap;  // RGB888 format (3 bytes per pixel)
    
    // Calculate dimensions
    uint16_t w = rect->right - rect->left + 1;
    uint16_t h = rect->bottom - rect->top + 1;
    uint16_t x = rect->left;
    uint16_t y = rect->top;
    
    // Bounds check
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
        return 1;
    }
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    
    // Set display window for this MCU block
    display_set_window(x, y, w, h);
    
    // Convert RGB888 to RGB565 and write scanline by scanline
    for (uint16_t row = 0; row < h; row++) {
        uint8_t *src_row = &src[row * w * 3];
        
        for (uint16_t col = 0; col < w; col++) {
            uint8_t r = src_row[col * 3];
            uint8_t g = src_row[col * 3 + 1];
            uint8_t b = src_row[col * 3 + 2];
            
            // Pack RGB565 and byte-swap (big-endian for ST7789)
            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            mcu_line_buffer[col] = (rgb565 >> 8) | (rgb565 << 8);
        }
        
        // Write this scanline
        display_write_preswapped(mcu_line_buffer, w);
    }
    
    return 1;
}

/**
 * Decode and display a single JPEG frame
 */
static int decode_jpeg_frame(const uint8_t *jpeg_data, size_t jpeg_size)
{
    JDEC jdec;
    JRESULT res;
    jpeg_decode_ctx_t ctx;
    
    // Initialize context
    ctx.jpeg_data = jpeg_data;
    ctx.jpeg_size = jpeg_size;
    ctx.jpeg_pos = 0;
    
    // Prepare decoder
    res = jd_prepare(&jdec, tjpgd_input_func, tjpgd_work, TJPGD_WORKSPACE_SIZE, &ctx);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "JPEG prepare failed: %d", res);
        return -1;
    }
    
    // Decompress and draw (scale 0 = 1:1)
    res = jd_decomp(&jdec, tjpgd_output_func, 0);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "JPEG decompress failed: %d", res);
        return -1;
    }
    
    // Wait for DMA to complete
    display_wait_done();
    
    return 0;
}

/**
 * Find next JPEG frame in MJPEG stream
 * Returns pointer to frame start and sets frame_size
 */
static const uint8_t* find_next_jpeg(const uint8_t *data, size_t data_size, 
                                     size_t *offset, size_t *frame_size)
{
    size_t pos = *offset;
    
    // Find SOI marker (0xFF 0xD8)
    while (pos < data_size - 1) {
        if (data[pos] == 0xFF && data[pos + 1] == 0xD8) {
            break;
        }
        pos++;
    }
    
    if (pos >= data_size - 1) {
        return NULL;  // No more frames
    }
    
    const uint8_t *frame_start = &data[pos];
    size_t frame_pos = pos + 2;
    
    // Find EOI marker (0xFF 0xD9)
    while (frame_pos < data_size - 1) {
        if (data[frame_pos] == 0xFF && data[frame_pos + 1] == 0xD9) {
            frame_pos += 2;  // Include EOI in frame
            break;
        }
        frame_pos++;
    }
    
    if (frame_pos >= data_size) {
        return NULL;  // Incomplete frame
    }
    
    *frame_size = frame_pos - pos;
    *offset = frame_pos;
    
    return frame_start;
}

/**
 * Play the FIESTA video
 */
void play_fiesta_video(void)
{
    ESP_LOGI(TAG, "Playing FIESTA video (%zu bytes)", fiesta_video_size);
    
    size_t offset = 0;
    size_t frame_size;
    const uint8_t *frame_data;
    int frame_count = 0;
    
    int64_t video_start_time = esp_timer_get_time();
    
    // Clear screen
    display_fill(0x0000);
    
    // Decode and display frames
    while ((frame_data = find_next_jpeg(fiesta_video, fiesta_video_size, &offset, &frame_size)) != NULL) {
        int64_t frame_start = esp_timer_get_time();
        
        // Decode and display frame
        if (decode_jpeg_frame(frame_data, frame_size) != 0) {
            ESP_LOGE(TAG, "Failed to decode frame %d", frame_count);
            break;
        }
        
        frame_count++;
        
        // Frame timing
        int64_t frame_end = esp_timer_get_time();
        int64_t target_us = frame_count * (1000000 / TARGET_FPS);
        int64_t actual_us = frame_end - video_start_time;
        int64_t delay_us = target_us - actual_us;
        
        if (delay_us > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
        }
    }
    
    int64_t total_time = (esp_timer_get_time() - video_start_time) / 1000;
    ESP_LOGI(TAG, "Video complete: %d frames in %lld ms (%.1f fps)", 
             frame_count, total_time, (frame_count * 1000.0f) / total_time);
}
