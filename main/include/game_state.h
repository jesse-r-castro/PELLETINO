#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pac-Man memory map addresses
#define PACMAN_ADDR_LIVES       0x4E14  // Number of lives remaining (in game RAM)
#define PACMAN_ADDR_GAME_STATE  0x4E00  // Game state (0x01=attract, 0x02=starting, 0x03=playing, etc.)

// Check if attract mode is starting (transition into mode 0x01)
bool check_attract_mode_start(const uint8_t *memory);

// Get current lives count
uint8_t get_lives_count(const uint8_t *memory);

#ifdef __cplusplus
}
#endif

#endif // GAME_STATE_H
