/*
 * PELLETINO - Pac-Man Arcade Simulator for ESP32-C6
 *
 * Main entry point
 */

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "audio_hal.h"
#include "display.h"
#include "game_state.h"
#include "pacman_hw.h"
#include "pacman_input.h"
#include "qmi8658.h"
#include "z80_cpu.h"

// Forward declare video player
extern "C" int play_fiesta_video(void);

// Game selection - uncomment one:
#define GAME_PACMAN
// #define GAME_MSPACMAN

// Include converted ROM data based on game selection
#ifdef GAME_MSPACMAN
#include "mspacman_cmap.h"
#include "mspacman_rom.h"
#include "mspacman_spritemap.h"
#include "mspacman_tilemap.h"
#include "mspacman_wavetable.h"
#define GAME_NAME "Ms. Pac-Man"
#define GAME_ROM mspacman_rom
#define GAME_TILES mspacman_5e
#define GAME_SPRITES mspacman_sprites
#define GAME_COLORMAP mspacman_colormap
#define GAME_WAVETABLE mspacman_wavetable
#else
#include "pacman_cmap.h"
#include "pacman_rom.h"
#include "pacman_spritemap.h"
#include "pacman_tilemap.h"
#include "pacman_wavetable.h"
#define GAME_NAME "Pac-Man"
#define GAME_ROM pacman_rom
#define GAME_TILES pacman_5e
#define GAME_SPRITES pacman_sprites
#define GAME_COLORMAP pacman_colormap
#define GAME_WAVETABLE pacman_wavetable
#endif

// Debug logging flag - set to 1 for serial output, 0 for silent (battery saving)
#define PELLETINO_DEBUG 0

static const char *TAG = "PELLETINO";

// Frame timing
static constexpr uint32_t FRAME_TIME_US = 16667; // 60 Hz = 16.667ms

// Main emulation state
static bool running = false;

extern "C" void app_main(void) {
#if !PELLETINO_DEBUG
  esp_log_level_set("*", ESP_LOG_NONE);
#endif
  ESP_LOGI(TAG, "PELLETINO starting - %s", GAME_NAME);
  ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

  // Initialize display
  ESP_LOGI(TAG, "Initializing display...");
  display_init();

  // Initialize audio (ES8311 + I2S)
  ESP_LOGI(TAG, "Initializing audio...");
  audio_init();

  // Initialize Z80 CPU emulator
  ESP_LOGI(TAG, "Initializing Z80 CPU...");
  z80_init();

  // Initialize Pac-Man hardware emulation
  ESP_LOGI(TAG, "Initializing Pac-Man hardware...");
  pacman_hw_init();

  // Load ROM and graphics data
  ESP_LOGI(TAG, "Loading ROM data...");
  pacman_set_rom(GAME_ROM);
  pacman_set_tiles(GAME_TILES);
  pacman_set_sprites(&GAME_SPRITES[0][0][0]);
  pacman_set_palette(&GAME_COLORMAP[0][0]);
  pacman_set_wavetable(&GAME_WAVETABLE[0][0]);
  pacman_load_roms();

  ESP_LOGI(TAG, "Free heap after init: %lu bytes", esp_get_free_heap_size());

  running = true;
  uint64_t frame_start;
  uint64_t frame_count = 0;
  bool game_over_video_played = false;

  // Battery optimization: Audio silence detection
  uint32_t silence_frames = 0;
  const uint32_t SILENCE_THRESHOLD = 120; // 2 seconds @ 60fps
  bool audio_powered = true;

  // Battery optimization: Adaptive backlight dimming
  uint32_t idle_frames = 0;
  const uint32_t IDLE_DIM_THRESHOLD = 1800; // 30 seconds @ 60fps
  uint8_t current_brightness = DISPLAY_BRIGHTNESS_ACTIVE;

  // Battery optimization: CPU frequency scaling
  bool cpu_low_power = false;

  while (running) {
    frame_start = esp_timer_get_time();

    // 1. Run Z80 CPU for one frame worth of cycles (~50,000 @ 3MHz / 60Hz)
    pacman_run_frame();

    // 2. Render display (uses DMA, interleaved with audio)
    pacman_render_screen();

    // 3. Update audio buffer
    audio_update();

    // 4. Poll input
    pacman_poll_input();

    // 5. Battery optimization: Detect audio silence and power down amplifier
    // Also respect mute state - keep amplifier off when muted
    extern bool audio_get_mute(void);
    bool is_muted = audio_get_mute();
    bool is_silent = true;
    uint8_t* sound_regs = audio_get_sound_registers();
    for (int ch = 0; ch < 3; ch++) {
      if (sound_regs[ch * 5 + 0x15] & 0x0F) { // Check volume for each channel
        is_silent = false;
        break;
      }
    }

    // When muted, always keep amplifier off
    if (is_muted) {
      if (audio_powered) {
        audio_set_power_state(false);
        audio_powered = false;
      }
      silence_frames = 0; // Reset counter
    } else if (is_silent) {
      silence_frames++;
      if (silence_frames == SILENCE_THRESHOLD && audio_powered) {
        audio_set_power_state(false);
        audio_powered = false;
      }
    } else {
      if (!audio_powered) {
        audio_set_power_state(true);
        audio_powered = true;
      }
      silence_frames = 0;
    }

    // 6. Battery optimization: CPU frequency scaling
    // Check game state to determine if actively playing
    const uint8_t* memory = pacman_get_memory();
    uint8_t game_mode = memory ? memory[PACMAN_ADDR_GAME_STATE - 0x4000] : 0;
    bool is_playing = (game_mode >= 0x02);  // 0x01=attract, 0x02+=active game
    
    if (is_playing && cpu_low_power) {
      // Switch to high performance for gameplay
      esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 160,
        .light_sleep_enable = false
      };
      esp_pm_configure(&pm_config);
      cpu_low_power = false;
      ESP_LOGI(TAG, "CPU frequency: 160MHz (active gameplay)");
    } else if (!is_playing && !cpu_low_power) {
      // Switch to low power for attract mode
      esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = true
      };
      esp_pm_configure(&pm_config);
      cpu_low_power = true;
      ESP_LOGI(TAG, "CPU frequency: 80MHz (attract mode)");
    }

    // 7. Battery optimization: Adaptive backlight dimming
    // Reset idle counter when actively playing (game mode >= 0x02)
    if (is_playing) {
      idle_frames = 0;
    } else {
      idle_frames++;
    }
    if (idle_frames >= IDLE_DIM_THRESHOLD) {
      if (current_brightness != DISPLAY_BRIGHTNESS_IDLE) {
        display_set_backlight(DISPLAY_BRIGHTNESS_IDLE);
        current_brightness = DISPLAY_BRIGHTNESS_IDLE;
        ESP_LOGI(TAG, "Backlight dimmed to 25%% (idle)");
      }
    } else if (current_brightness != DISPLAY_BRIGHTNESS_ACTIVE) {
      display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);
      current_brightness = DISPLAY_BRIGHTNESS_ACTIVE;
      ESP_LOGI(TAG, "Backlight restored to 50%% (active)");
    }

    // 8. Trigger VBLANK interrupt if enabled
    pacman_vblank_interrupt();

    // 9. Check for attract mode start (after arcade boot or after game over) and play video
    if (check_attract_mode_start(pacman_get_memory())) {
      ESP_LOGI(TAG, "Attract mode starting - playing FIESTA video...");
      // Temporarily boost CPU for video decode (runs at 80MHz otherwise in attract)
      esp_pm_config_t pm_video = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 160,
        .light_sleep_enable = false
      };
      esp_pm_configure(&pm_video);
      play_fiesta_video();
      // Restore low power for attract mode
      esp_pm_config_t pm_low = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = true
      };
      esp_pm_configure(&pm_low);
      // Clear any accumulated credits so attract mode plays demo
      // instead of waiting for START button press
      clear_credits(pacman_get_memory_rw());
      ESP_LOGI(TAG, "Video complete, attract mode will continue");
    }

    // Frame timing - wait for 16.667ms total
    uint64_t elapsed = esp_timer_get_time() - frame_start;
    if (elapsed < FRAME_TIME_US) {
      vTaskDelay(pdMS_TO_TICKS((FRAME_TIME_US - elapsed) / 1000));
    }

    frame_count++;
    if (frame_count % 300 == 0) { // Every 5 seconds
      ESP_LOGI(TAG, "Frame %llu, elapsed: %llu us", frame_count, elapsed);
    }
  }
}
