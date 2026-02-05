/*
 * fiesta_video.c - Optimized MJPEG Video Player for FIESTA26
 *
 * Optimizations:
 * - Full-frame buffer then chunked DMA transfers
 * - Optimized RGB888→RGB565 conversion
 * - Frame skipping when behind
 */

#include "esp32c6/rom/tjpgd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Forward declare display functions
extern void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
extern void display_write_preswapped(const uint16_t *data, uint32_t len);
extern void display_wait_done(void);
extern void display_fill(uint16_t color);

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 280

static const char *TAG = "FIESTA_VIDEO";

// Video playback settings
#define TARGET_FPS 24
#define FRAME_TIME_US (1000000 / TARGET_FPS)
#define MAX_BEHIND_US 100000

// Chunk size for DMA transfers (must fit in display driver's DMA buffer)
#define ROWS_PER_CHUNK 14
#define CHUNK_PIXELS (DISPLAY_WIDTH * ROWS_PER_CHUNK)

// External video data
#include "../../movie/fiesta_data.h"

// TinyJPEG work buffer
#define TJPGD_WORKSPACE_SIZE 4096
static uint8_t tjpgd_work[TJPGD_WORKSPACE_SIZE];

// Full-frame buffer
#define FRAME_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT)
static uint16_t *frame_buffer = NULL;

// Context for JPEG decoder
typedef struct {
  const uint8_t *jpeg_data;
  size_t jpeg_size;
  size_t jpeg_pos;
} jpeg_decode_ctx_t;

/**
 * TinyJPEG input function
 */
static unsigned int tjpgd_input_func(JDEC *jd, uint8_t *buff,
                                     unsigned int nbyte) {
  jpeg_decode_ctx_t *ctx = (jpeg_decode_ctx_t *)jd->device;

  if (buff == NULL) {
    ctx->jpeg_pos += nbyte;
    return nbyte;
  }

  if (ctx->jpeg_pos + nbyte > ctx->jpeg_size) {
    nbyte = ctx->jpeg_size - ctx->jpeg_pos;
  }

  memcpy(buff, ctx->jpeg_data + ctx->jpeg_pos, nbyte);
  ctx->jpeg_pos += nbyte;

  return nbyte;
}

/**
 * Optimized RGB888 to RGB565 conversion
 */
static inline void convert_rgb888_to_rgb565_fast(const uint8_t *src,
                                                 uint16_t *dst,
                                                 uint16_t count) {
  while (count--) {
    uint16_t rgb565 =
        ((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | (src[2] >> 3);
    *dst++ = __builtin_bswap16(rgb565);
    src += 3;
  }
}

/**
 * TinyJPEG output function - writes to frame buffer
 */
static unsigned int tjpgd_output_func(JDEC *jd, void *bitmap, JRECT *rect) {
  uint8_t *src = (uint8_t *)bitmap;

  uint16_t x = rect->left;
  uint16_t y = rect->top;
  uint16_t w = rect->right - rect->left + 1;
  uint16_t h = rect->bottom - rect->top + 1;

  if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return 1;
  }
  if (x + w > DISPLAY_WIDTH)
    w = DISPLAY_WIDTH - x;
  if (y + h > DISPLAY_HEIGHT)
    h = DISPLAY_HEIGHT - y;

  // Write MCU to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    uint16_t *dst = frame_buffer + (y + row) * DISPLAY_WIDTH + x;
    const uint8_t *row_src = src + row * (rect->right - rect->left + 1) * 3;
    convert_rgb888_to_rgb565_fast(row_src, dst, w);
  }

  return 1;
}

/**
 * Send frame buffer to display in chunks
 */
static void send_frame_to_display(void) {
  uint16_t y = 0;

  while (y < DISPLAY_HEIGHT) {
    uint16_t rows = ROWS_PER_CHUNK;
    if (y + rows > DISPLAY_HEIGHT) {
      rows = DISPLAY_HEIGHT - y;
    }

    display_set_window(0, y, DISPLAY_WIDTH, rows);
    display_write_preswapped(frame_buffer + y * DISPLAY_WIDTH,
                             DISPLAY_WIDTH * rows);
    display_wait_done();

    y += rows;
  }
}

/**
 * Decode and display a single JPEG frame
 */
static int decode_jpeg_frame(const uint8_t *jpeg_data, size_t jpeg_size) {
  JDEC jdec;
  JRESULT res;
  jpeg_decode_ctx_t ctx;

  ctx.jpeg_data = jpeg_data;
  ctx.jpeg_size = jpeg_size;
  ctx.jpeg_pos = 0;

  memset(frame_buffer, 0, FRAME_BUFFER_SIZE * sizeof(uint16_t));

  res = jd_prepare(&jdec, tjpgd_input_func, tjpgd_work, TJPGD_WORKSPACE_SIZE,
                   &ctx);
  if (res != JDR_OK) {
    ESP_LOGE(TAG, "JPEG prepare failed: %d", res);
    return -1;
  }

  res = jd_decomp(&jdec, tjpgd_output_func, 0);
  if (res != JDR_OK) {
    ESP_LOGE(TAG, "JPEG decompress failed: %d", res);
    return -1;
  }

  send_frame_to_display();

  return 0;
}

/**
 * Find next JPEG frame in MJPEG stream
 */
static const uint8_t *find_next_jpeg(const uint8_t *data, size_t data_size,
                                     size_t *offset, size_t *frame_size) {
  size_t pos = *offset;

  while (pos < data_size - 1) {
    if (data[pos] == 0xFF && data[pos + 1] == 0xD8) {
      break;
    }
    pos++;
  }

  if (pos >= data_size - 1) {
    return NULL;
  }

  const uint8_t *frame_start = &data[pos];
  size_t frame_pos = pos + 2;

  while (frame_pos < data_size - 1) {
    if (data[frame_pos] == 0xFF && data[frame_pos + 1] == 0xD9) {
      frame_pos += 2;
      break;
    }
    frame_pos++;
  }

  if (frame_pos >= data_size) {
    return NULL;
  }

  *frame_size = frame_pos - pos;
  *offset = frame_pos;

  return frame_start;
}

/**
 * Play the FIESTA video
 */
void play_fiesta_video(void) {
  ESP_LOGI(TAG, "Playing FIESTA video (%zu bytes)", fiesta_video_size);

  frame_buffer = (uint16_t *)heap_caps_malloc(
      FRAME_BUFFER_SIZE * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (!frame_buffer) {
    ESP_LOGE(TAG, "Failed to allocate frame buffer!");
    return;
  }

  size_t offset = 0;
  size_t frame_size;
  const uint8_t *frame_data;
  int frame_count = 0;
  int frames_skipped = 0;

  int64_t video_start_time = esp_timer_get_time();

  display_fill(0x0000);

  while ((frame_data = find_next_jpeg(fiesta_video, fiesta_video_size, &offset,
                                      &frame_size)) != NULL) {
    int64_t target_us = (int64_t)frame_count * FRAME_TIME_US;
    int64_t actual_us = esp_timer_get_time() - video_start_time;

    if (actual_us > target_us + MAX_BEHIND_US) {
      frame_count++;
      frames_skipped++;
      continue;
    }

    if (decode_jpeg_frame(frame_data, frame_size) != 0) {
      ESP_LOGE(TAG, "Failed to decode frame %d", frame_count);
      break;
    }

    frame_count++;

    int64_t frame_end = esp_timer_get_time();
    actual_us = frame_end - video_start_time;
    target_us = (int64_t)frame_count * FRAME_TIME_US;
    int64_t delay_us = target_us - actual_us;

    if (delay_us > 1000) {
      vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
    }
  }

  int64_t total_time = (esp_timer_get_time() - video_start_time) / 1000;
  float actual_fps = (frame_count * 1000.0f) / total_time;
  ESP_LOGI(TAG, "Video complete: %d frames (%d skipped) in %lld ms (%.1f fps)",
           frame_count, frames_skipped, total_time, actual_fps);

  // Display black frame to clear any leftover video pixels
  display_fill(0x0000);

  heap_caps_free(frame_buffer);
  frame_buffer = NULL;
}
