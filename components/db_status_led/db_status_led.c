#include "db_status_led.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_indicator.h"
#include "led_indicator_ledc.h"

static const char *TAG = "db_status_led";

/* led_indicator brightness scale is 0-255 (led_types.h). */
#define BRIGHTNESS(pct)  ((uint32_t)(pct) * 255U / 100U)
#define FULL             BRIGHTNESS(CONFIG_DB_LED_BRIGHTNESS_FULL_PCT)
#define DIM              BRIGHTNESS(CONFIG_DB_LED_BRIGHTNESS_DIM_PCT)

#define LEVEL(value, ms) { LED_BLINK_BRIGHTNESS, (value), (ms) }
#define ON(ms)           LEVEL(FULL, (ms))
#define OFF(ms)          LEVEL(0, (ms))
#define LOOP             { LED_BLINK_LOOP, 0, 0 }
#define STOP             { LED_BLINK_STOP, 0, 0 }

#define FAST_HALF        (CONFIG_DB_LED_FAST_BLINK_PERIOD_MS / 2)
#define MEDIUM_HALF      (CONFIG_DB_LED_MEDIUM_BLINK_PERIOD_MS / 2)
#define SLOW_HALF        (CONFIG_DB_LED_SLOW_BLINK_PERIOD_MS / 2)
#define PULSE_ON         CONFIG_DB_LED_PULSE_ON_MS
#define PULSE_GAP        CONFIG_DB_LED_PULSE_GAP_MS
#define PULSE_CYCLE      CONFIG_DB_LED_PULSE_CYCLE_MS
#define SHORT_FLASH      CONFIG_DB_LED_SHORT_FLASH_MS
#define SPARSE_INTERVAL  CONFIG_DB_LED_SPARSE_FLASH_INTERVAL_MS
#define HOLD_MS          1000  /* step length of solid patterns; they loop until stopped */

/* led_indicator runs each step on a FreeRTOS timer with pdMS_TO_TICKS(hold_time_ms); a step shorter than one tick
 * becomes a 0-tick period, which FreeRTOS rejects with an assert. A hold time of 0 means "apply and continue". */
#define TICK_MS (1000 / CONFIG_FREERTOS_HZ)
#define AT_LEAST_ONE_TICK(ms) ((ms) >= TICK_MS)
_Static_assert(AT_LEAST_ONE_TICK(SHORT_FLASH) && AT_LEAST_ONE_TICK(PULSE_ON) && AT_LEAST_ONE_TICK(PULSE_GAP) &&
               AT_LEAST_ONE_TICK(FAST_HALF) && AT_LEAST_ONE_TICK(MEDIUM_HALF) && AT_LEAST_ONE_TICK(SLOW_HALF) &&
               AT_LEAST_ONE_TICK(PULSE_CYCLE - 3 * PULSE_ON - 2 * PULSE_GAP) &&
               AT_LEAST_ONE_TICK(SPARSE_INTERVAL - SHORT_FLASH) && AT_LEAST_ONE_TICK(PULSE_CYCLE - SHORT_FLASH),
               "every LED pattern step must last at least one FreeRTOS tick");

/* Reject timing combinations that cannot form the pattern, at build time. */
_Static_assert(PULSE_CYCLE > 3 * PULSE_ON + 2 * PULSE_GAP,
               "DB_LED_PULSE_CYCLE_MS must exceed three pulses and two gaps");
_Static_assert(SPARSE_INTERVAL > SHORT_FLASH, "DB_LED_SPARSE_FLASH_INTERVAL_MS must exceed DB_LED_SHORT_FLASH_MS");
_Static_assert(PULSE_CYCLE > SHORT_FLASH, "DB_LED_PULSE_CYCLE_MS must exceed DB_LED_SHORT_FLASH_MS");

static const blink_step_t s_fast_blink[] = { ON(FAST_HALF), OFF(FAST_HALF), LOOP };
static const blink_step_t s_medium_blink[] = { ON(MEDIUM_HALF), OFF(MEDIUM_HALF), LOOP };
static const blink_step_t s_slow_blink[] = { ON(SLOW_HALF), OFF(SLOW_HALF), LOOP };
static const blink_step_t s_solid[] = { ON(HOLD_MS), LOOP };
static const blink_step_t s_success[] = { ON(CONFIG_DB_LED_SUCCESS_MS), OFF(0), STOP };
static const blink_step_t s_key_press[] = { ON(SHORT_FLASH), OFF(0), STOP };
static const blink_step_t s_double_flash[] = {
    ON(PULSE_ON), OFF(PULSE_GAP), ON(PULSE_ON), OFF(PULSE_CYCLE - 2 * PULSE_ON - PULSE_GAP), LOOP,
};
static const blink_step_t s_triple_flash[] = {
    ON(PULSE_ON), OFF(PULSE_GAP), ON(PULSE_ON), OFF(PULSE_GAP), ON(PULSE_ON),
    OFF(PULSE_CYCLE - 3 * PULSE_ON - 2 * PULSE_GAP), LOOP,
};
static const blink_step_t s_solid_with_blink[] = { ON(PULSE_CYCLE - SHORT_FLASH), OFF(SHORT_FLASH), LOOP };
static const blink_step_t s_sparse_flash[] = { ON(SHORT_FLASH), OFF(SPARSE_INTERVAL - SHORT_FLASH), LOOP };
static const blink_step_t s_dim[] = { LEVEL(DIM, HOLD_MS), LOOP };
static const blink_step_t s_off[] = { OFF(HOLD_MS), LOOP };

/* Index = db_led_pattern_t = led_indicator priority (lower index wins). */
static const blink_step_t *s_patterns[DB_LED_PATTERN_COUNT] = {
    [DB_LED_ERROR] = s_fast_blink,
    [DB_LED_DOOR_OPEN] = s_solid,
    [DB_LED_SUCCESS] = s_success,
    [DB_LED_KEY_PRESS] = s_key_press,
    [DB_LED_NO_RESPONSE] = s_medium_blink,
    [DB_LED_BUTTON_CANCELLED] = s_triple_flash,
    [DB_LED_RINGING] = s_fast_blink,
    [DB_LED_PAIRING_HOLD] = s_medium_blink,
    [DB_LED_RESET_HOLD] = s_solid_with_blink,
    [DB_LED_PAIRING_MODE] = s_slow_blink,
    [DB_LED_PAIRING_WINDOW] = s_double_flash,
    [DB_LED_COMMAND_ENTRY] = s_slow_blink,
    [DB_LED_LOCKED] = s_sparse_flash,
    [DB_LED_UNPAIRED] = s_double_flash,
    [DB_LED_PAIRED_IDLE] = s_dim,
    [DB_LED_OFF] = s_off,
};

static led_indicator_handle_t s_led;
static esp_timer_handle_t s_stop_timers[DB_LED_PATTERN_COUNT];

static void stop_timer_cb(void *arg)
{
    db_status_led_stop((db_led_pattern_t)(intptr_t)arg);
}

esp_err_t db_status_led_init(int gpio_num)
{
    ESP_RETURN_ON_FALSE(s_led == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");

    const led_indicator_config_t config = {
        .blink_lists = s_patterns,
        .blink_list_num = DB_LED_PATTERN_COUNT,
    };
    const led_indicator_ledc_config_t ledc = {
#if CONFIG_DB_STATUS_LED_ACTIVE_HIGH
        .is_active_level_high = true,
#else
        .is_active_level_high = false,
#endif
        .timer_inited = false,  /* led_indicator configures the timer from its own Kconfig (LED Indicator menu) */
        .timer_num = LEDC_TIMER_0,
        .gpio_num = gpio_num,
        .channel = LEDC_CHANNEL_0,
    };
    ESP_RETURN_ON_ERROR(led_indicator_new_ledc_device(&config, &ledc, &s_led), TAG, "LED init failed");

    for (int i = 0; i < DB_LED_PATTERN_COUNT; i++) {
        const esp_timer_create_args_t args = {
            .callback = stop_timer_cb,
            .arg = (void *)(intptr_t)i,
            .name = "db_led_stop",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_stop_timers[i]), TAG, "timer create failed");
    }
    return led_indicator_start(s_led, DB_LED_OFF);
}

static bool valid(db_led_pattern_t pattern)
{
    if (s_led == NULL || pattern >= DB_LED_PATTERN_COUNT) {
        ESP_LOGE(TAG, "pattern %d rejected: %s", (int)pattern,
                 s_led == NULL ? "not initialised" : "invalid pattern");
        return false;
    }
    return true;
}

void db_status_led_start(db_led_pattern_t pattern)
{
    if (valid(pattern)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_start(s_led, pattern));
    }
}

void db_status_led_stop(db_led_pattern_t pattern)
{
    if (!valid(pattern) || pattern == DB_LED_OFF) {  /* the base pattern stays active */
        return;
    }
    (void)esp_timer_stop(s_stop_timers[pattern]);  /* ESP_ERR_INVALID_STATE when not running is expected */
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_stop(s_led, pattern));
}

void db_status_led_start_for(db_led_pattern_t pattern, uint32_t duration_ms)
{
    if (!valid(pattern)) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_start(s_led, pattern));
    (void)esp_timer_stop(s_stop_timers[pattern]);  /* restart the duration if already running */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(s_stop_timers[pattern], (uint64_t)duration_ms * 1000U));
}
