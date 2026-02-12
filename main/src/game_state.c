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

bool check_attract_mode_start(const uint8_t *memory)
{
    frames_since_startup++;
    
    // Skip early frames to let arcade ROM boot and stabilize
    if (frames_since_startup < 180) {  // 3 seconds at 60 FPS
        return false;
    }
    
    uint8_t lives = memory[ADDR_LIVES];
    uint8_t game_mode = memory[ADDR_GAME_STATE];
    
    // Log state changes for debugging (every 5 seconds)
    if (frames_since_startup % 300 == 0) {
        ESP_LOGI(TAG, "Lives: %d, Mode: 0x%02x, LastMode: 0x%02x, Started: %d, VideoPlayed: %d", 
                 lives, game_mode, last_game_mode, game_has_started, video_played_this_session);
    }
    
    // Detect game start: mode transitions from 0x01 (attract) to 0x02 (starting)
    if (!game_has_started && game_mode == 0x02 && last_game_mode == 0x01) {
        ESP_LOGI(TAG, "Game starting! Mode: 0x%02x", game_mode);
        game_has_started = true;
        video_played_this_session = false;
    }
    
    // Detect attract mode start: mode transitions TO 0x01 (attract)
    // This happens after arcade boot OR after game over screen
    if (game_mode == 0x01 && last_game_mode != 0x01 && !video_played_this_session) {
        ESP_LOGI(TAG, "Attract mode starting! Last mode: 0x%02x", last_game_mode);
        video_played_this_session = true;
        if (game_has_started) {
            game_has_started = false;  // Reset for next game
        }
        last_lives = lives;
        last_game_mode = game_mode;
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

void clear_credits(uint8_t *memory)
{
    memory[PACMAN_ADDR_CREDITS - 0x4000] = 0;  // Clear credit count
    memory[PACMAN_ADDR_COINS - 0x4000] = 0;    // Clear partial coin count
    ESP_LOGI(TAG, "Credits and coins cleared");
}
