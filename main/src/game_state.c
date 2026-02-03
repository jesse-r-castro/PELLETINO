/*
 * game_state.c - Game state monitoring for Pac-Man
 * 
 * Detects game over by monitoring multiple RAM addresses:
 * - 0x4E14: Lives remaining
 * - 0x4E00: Game mode/state
 */

#include "game_state.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "GAMESTATE";

static uint8_t last_lives = 0;
static uint8_t last_game_mode = 0;
static bool game_has_started = false;
static bool video_played_this_session = false;
static uint32_t frames_since_startup = 0;

// Pac-Man memory addresses
#define ADDR_LIVES       (PACMAN_ADDR_LIVES - 0x4000)
#define ADDR_GAME_STATE  (PACMAN_ADDR_GAME_STATE - 0x4000)

bool check_game_over(const uint8_t *memory)
{
    frames_since_startup++;
    
    // Skip early frames to let game initialize
    if (frames_since_startup < 180) {  // 3 seconds at 60 FPS
        return false;
    }
    
    uint8_t lives = memory[ADDR_LIVES];
    uint8_t game_mode = memory[ADDR_GAME_STATE];
    
    // Log state changes for debugging (every 5 seconds)
    if (frames_since_startup % 300 == 0) {
        ESP_LOGI(TAG, "Lives: %d, Mode: 0x%02x, Started: %d, VideoPlayed: %d", 
                 lives, game_mode, game_has_started, video_played_this_session);
    }
    
    // Detect game start: lives become 3 (or 2-5 depending on DIP switches)
    if (!game_has_started && lives >= 2 && lives <= 5 && lives != last_lives) {
        ESP_LOGI(TAG, "Game started! Lives: %d", lives);
        game_has_started = true;
        video_played_this_session = false;
    }
    
    // Detect game over: lives drop to 0 after game started
    if (game_has_started && !video_played_this_session && lives == 0 && last_lives > 0) {
        ESP_LOGI(TAG, "Game Over detected! Last lives: %d", last_lives);
        video_played_this_session = true;
        game_has_started = false;  // Reset for next game
        last_lives = 0;
        return true;
    }
    
    last_lives = lives;
    last_game_mode = game_mode;
    
    return false;
}

uint8_t get_lives_count(const uint8_t *memory)
{
    return memory[ADDR_LIVES];
}
