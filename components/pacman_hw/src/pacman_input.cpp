/*
 * pacman_input.cpp - Pac-Man Input Handling for FIESTA26
 *
 * Uses GPIO buttons and IMU for tilt control
 */

#include "pacman_input.h"
#include "qmi8658.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PACMAN_INPUT";

// FIESTA26 button pins
#define PIN_BTN_BOOT    GPIO_NUM_9    // BOOT button
#define PIN_BTN_PWR     GPIO_NUM_18   // PWR button
#define PIN_BAT_EN      GPIO_NUM_15   // Battery enable (keep HIGH for power)

// Tilt thresholds for dead zone / hysteresis
// These define when tilt is recognized as a direction
#define TILT_THRESHOLD_ON   25   // Threshold to activate direction
#define TILT_THRESHOLD_OFF  15   // Threshold to deactivate (hysteresis)

// Power button long press threshold (in update cycles at ~60fps)
#define PWR_LONG_PRESS_FRAMES  60  // ~1 second

// Current input state
static uint8_t current_buttons = 0;

// Virtual coin/start state machine (single button triggers both)
static uint32_t virtual_coin_timer = 0;
static int virtual_coin_state = 0;

// IMU tilt state with hysteresis
static bool tilt_up_active = false;
static bool tilt_down_active = false;
static bool tilt_left_active = false;
static bool tilt_right_active = false;

// Power button state for long press detection
static uint32_t pwr_press_counter = 0;

void pacman_input_init(void)
{
    ESP_LOGI(TAG, "Initializing input");

    // Configure BAT_EN (GPIO15) as output HIGH to maintain battery power
    gpio_config_t bat_conf = {};
    bat_conf.pin_bit_mask = (1ULL << PIN_BAT_EN);
    bat_conf.mode = GPIO_MODE_OUTPUT;
    bat_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    bat_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bat_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&bat_conf);
    gpio_set_level(PIN_BAT_EN, 1);  // Keep power on
    ESP_LOGI(TAG, "BAT_EN (GPIO15) set HIGH - battery power maintained");

    // Configure button GPIOs
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Initialize IMU
    if (qmi8658_init()) {
        // Auto-calibrate on startup (assumes device is roughly flat)
        qmi8658_calibrate();
        ESP_LOGI(TAG, "IMU tilt control enabled");
    } else {
        ESP_LOGW(TAG, "IMU not available, using buttons only");
    }

    ESP_LOGI(TAG, "Input initialized (BOOT=coin/start, PWR long-press=power off)");
}

void pacman_input_update(void)
{
    current_buttons = 0;

    // Read physical buttons (active low)
    bool boot_pressed = (gpio_get_level(PIN_BTN_BOOT) == 0);
    bool pwr_pressed = (gpio_get_level(PIN_BTN_PWR) == 0);

    // BOOT button: Coin + Start (with timing)
    if (boot_pressed) {
        current_buttons |= BTN_COIN;
    }

    // PWR button: Long press to power off
    if (pwr_pressed) {
        pwr_press_counter++;
        if (pwr_press_counter == PWR_LONG_PRESS_FRAMES) {
            ESP_LOGI(TAG, "Power button long press - shutting down");
            gpio_set_level(PIN_BAT_EN, 0);  // Cut power
            // Device will power off if on battery
            vTaskDelay(pdMS_TO_TICKS(1000));  // Wait in case USB powered
        }
    } else {
        pwr_press_counter = 0;  // Reset counter when released
    }

    // IMU tilt control with hysteresis
    if (qmi8658_is_initialized()) {
        int8_t pitch, roll;
        qmi8658_get_tilt(&pitch, &roll);

        // Debug: log tilt values periodically
        static int debug_counter = 0;
        if (++debug_counter >= 60) {  // Every ~1 second at 60fps
            ESP_LOGI(TAG, "Tilt: pitch=%d roll=%d", pitch, roll);
            debug_counter = 0;
        }

        // Swap pitch mapping: negative pitch = tilt toward you = UP
        // (Originally had positive = UP but IMU orientation is opposite)
        int8_t up_down = -pitch;

        // Pitch controls UP/DOWN
        // Apply hysteresis to prevent jitter at threshold
        if (tilt_up_active) {
            if (up_down < TILT_THRESHOLD_OFF) {
                tilt_up_active = false;
            }
        } else {
            if (up_down >= TILT_THRESHOLD_ON) {
                tilt_up_active = true;
                tilt_down_active = false;  // Can't be both
            }
        }

        if (tilt_down_active) {
            if (up_down > -TILT_THRESHOLD_OFF) {
                tilt_down_active = false;
            }
        } else {
            if (up_down <= -TILT_THRESHOLD_ON) {
                tilt_down_active = true;
                tilt_up_active = false;
            }
        }

        // Roll controls LEFT/RIGHT (inverted)
        int8_t left_right = -roll;
        
        if (tilt_left_active) {
            if (left_right > -TILT_THRESHOLD_OFF) {
                tilt_left_active = false;
            }
        } else {
            if (left_right <= -TILT_THRESHOLD_ON) {
                tilt_left_active = true;
                tilt_right_active = false;
            }
        }

        if (tilt_right_active) {
            if (left_right < TILT_THRESHOLD_OFF) {
                tilt_right_active = false;
            }
        } else {
            if (left_right >= TILT_THRESHOLD_ON) {
                tilt_right_active = true;
                tilt_left_active = false;
            }
        }

        // Apply tilt directions to buttons
        if (tilt_up_active)    current_buttons |= BTN_UP;
        if (tilt_down_active)  current_buttons |= BTN_DOWN;
        if (tilt_left_active)  current_buttons |= BTN_LEFT;
        if (tilt_right_active) current_buttons |= BTN_RIGHT;
    }

    // Virtual coin/start state machine
    static uint32_t last_time = 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    (void)last_time;  // Unused warning suppression

    switch (virtual_coin_state) {
        case 0:  // Idle
            if (current_buttons & BTN_COIN) {
                virtual_coin_state = 1;
                virtual_coin_timer = now;
            }
            break;
        case 1:  // Coin pressed, wait 100ms
            if (now - virtual_coin_timer > 100) {
                virtual_coin_state = 2;
                virtual_coin_timer = now;
            }
            break;
        case 2:  // Coin released, wait 500ms
            if (now - virtual_coin_timer > 500) {
                virtual_coin_state = 3;
                virtual_coin_timer = now;
            }
            break;
        case 3:  // Start pressed, wait 100ms
            if (now - virtual_coin_timer > 100) {
                virtual_coin_state = 4;
                virtual_coin_timer = now;
            }
            break;
        case 4:  // Wait for button release
            if (!(current_buttons & BTN_COIN)) {
                virtual_coin_state = 0;
            }
            break;
    }
}

uint8_t pacman_read_in0(void)
{
    /*
     * IN0 port layout (directly from Galagino):
     *   bit 0: UP
     *   bit 1: LEFT
     *   bit 2: RIGHT
     *   bit 3: DOWN
     *   bit 4: unused
     *   bit 5: COIN
     *   bit 6: unused
     *   bit 7: unused
     *
     * Returns active-low (0 = pressed)
     */
    uint8_t retval = 0xFF;

    // Joystick directions
    if (current_buttons & BTN_UP)    retval &= ~0x01;
    if (current_buttons & BTN_LEFT)  retval &= ~0x02;
    if (current_buttons & BTN_RIGHT) retval &= ~0x04;
    if (current_buttons & BTN_DOWN)  retval &= ~0x08;

    // Coin (virtual state machine)
    if (virtual_coin_state == 1) {
        retval &= ~0x20;  // Coin active during state 1
    }

    return retval;
}

uint8_t pacman_read_in1(void)
{
    /*
     * IN1 port layout:
     *   bit 5: 1P START
     *
     * Returns active-low (0 = pressed)
     */
    uint8_t retval = 0xFF;

    // Start (virtual state machine)
    if (virtual_coin_state == 3 || virtual_coin_state == 4) {
        retval &= ~0x20;  // Start active during states 3-4
    }

    return retval;
}
