#include "input.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "codes.h"
#include "db_status_led.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "input";

#define BUFFER_SIZE 48
#define MAX_TOKENS  5

static char s_buffer[BUFFER_SIZE];
static size_t s_length;
static command_handler_t s_handler;
static esp_timer_handle_t s_timeout_timer;
static void (*s_post_timeout)(void);

/* Wrong-code lockout (SPEC 4.1). */
static int s_failures;
static int s_lockout_level;
static int64_t s_locked_until_us;

static void timeout_cb(void *arg)
{
    s_post_timeout();
}

static void clear(void)
{
    s_length = 0;
    s_buffer[0] = '\0';
    (void)esp_timer_stop(s_timeout_timer);  /* ESP_ERR_INVALID_STATE when not running is expected */
    db_status_led_stop(DB_LED_COMMAND_ENTRY);
}

static void show_error(void)
{
    db_status_led_start_for(DB_LED_ERROR, CONFIG_DB_LED_ERROR_MS);
}

static bool locked(void)
{
    return esp_timer_get_time() < s_locked_until_us;
}

static void code_accepted(void)
{
    s_failures = 0;
    s_lockout_level = 0;
}

static void wrong_code(void)
{
    show_error();
    if (++s_failures < CONFIG_DB_LOCKOUT_ATTEMPTS) {
        return;
    }
    /* First lockout CONFIG_DB_LOCKOUT_BASE_S, multiplied for each further lockout, capped at the maximum.
     * The failure count restarts after each lockout; a correct code resets the level. */
    int64_t seconds = CONFIG_DB_LOCKOUT_BASE_S;
    for (int i = 0; i < s_lockout_level && seconds < CONFIG_DB_LOCKOUT_MAX_S; i++) {
        seconds *= CONFIG_DB_LOCKOUT_MULTIPLIER;
    }
    if (seconds > CONFIG_DB_LOCKOUT_MAX_S) {
        seconds = CONFIG_DB_LOCKOUT_MAX_S;
    }
    s_lockout_level++;
    s_failures = 0;
    s_locked_until_us = esp_timer_get_time() + seconds * 1000000;
    db_status_led_start_for(DB_LED_LOCKED, (uint32_t)seconds * 1000U);
    ESP_LOGW(TAG, "keypad locked for %d s", (int)seconds);
}

static bool all_digits(const char *text)
{
    if (*text == '\0') {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

static bool parse_apartment(const char *text, uint8_t *apartment)
{
    if (!all_digits(text) || strlen(text) > 3) {
        return false;
    }
    long value = strtol(text, NULL, 10);
    if (value < CONFIG_DB_APARTMENT_MIN || value > CONFIG_DB_APARTMENT_MAX) {
        return false;
    }
    *apartment = (uint8_t)value;
    return true;
}

static void emit(command_type_t type, uint8_t apartment, const char *code)
{
    command_t command = { .type = type, .apartment = apartment };
    if (code) {
        strlcpy(command.code, code, sizeof(command.code));
    }
    s_handler(&command);
}

/* "<admin>*<cmd>*<arg>[*<arg>]" after the admin code was verified. */
static void admin_command(char **tokens, int count)
{
    uint8_t apartment;
    const char *command = tokens[1];
    if (strcmp(command, "1") == 0 && count == 3 && parse_apartment(tokens[2], &apartment)) {
        emit(COMMAND_PAIR, apartment, NULL);
    } else if (strcmp(command, "2") == 0 && count == 4 && strcmp(tokens[2], tokens[3]) == 0 &&
               parse_apartment(tokens[2], &apartment)) {
        emit(COMMAND_DELETE, apartment, NULL);
    } else if (strcmp(command, "3") == 0 && count == 4 && strcmp(tokens[2], tokens[3]) == 0 &&
               codes_is_valid(tokens[2], CONFIG_DB_USER_CODE_LENGTH)) {
        emit(COMMAND_SET_USER_CODE, 0, tokens[2]);
    } else if (strcmp(command, "4") == 0 && count == 4 && strcmp(tokens[2], tokens[3]) == 0 &&
               codes_is_valid(tokens[2], CONFIG_DB_ADMIN_CODE_LENGTH)) {
        emit(COMMAND_SET_ADMIN_CODE, 0, tokens[2]);
    } else if (strcmp(command, "6") == 0 && count == 3 && codes_is_admin(tokens[2])) {
        emit(COMMAND_BUILDING_RESET, 0, NULL);
    } else {
        show_error();
    }
}

static void process(void)
{
    if (s_length == 0) {
        return;
    }
    if (s_buffer[0] != '*') {
        uint8_t apartment;
        if (parse_apartment(s_buffer, &apartment)) {
            emit(COMMAND_RING, apartment, NULL);  /* ringing is never blocked by the lockout */
        } else {
            show_error();
        }
        return;
    }
    if (s_length == 1) {
        return;  /* "* #": cancel */
    }
    if (locked()) {
        show_error();
        return;
    }

    char *tokens[MAX_TOKENS];
    int count = 0;
    char *save = NULL;
    for (char *token = strtok_r(s_buffer + 1, "*", &save); token; token = strtok_r(NULL, "*", &save)) {
        if (count == MAX_TOKENS) {
            show_error();
            return;
        }
        tokens[count++] = token;
    }
    if (count == 1 && codes_is_user(tokens[0])) {
        code_accepted();
        emit(COMMAND_OPEN_DOOR, 0, NULL);
    } else if (count >= 2 && codes_is_admin(tokens[0])) {
        code_accepted();
        admin_command(tokens, count);
    } else {
        wrong_code();
    }
}

esp_err_t input_init(command_handler_t handler, void (*post_timeout)(void))
{
    ESP_RETURN_ON_FALSE(handler && post_timeout, ESP_ERR_INVALID_ARG, TAG, "handlers are required");
    s_handler = handler;
    s_post_timeout = post_timeout;
    const esp_timer_create_args_t args = { .callback = timeout_cb, .name = "input_timeout" };
    return esp_timer_create(&args, &s_timeout_timer);
}

void input_key(char key)
{
    db_status_led_start(DB_LED_KEY_PRESS);
    if (key == '#') {
        process();
        clear();
        return;
    }
    if (s_length + 1 >= BUFFER_SIZE) {
        show_error();
        clear();
        return;
    }
    s_buffer[s_length++] = key;
    s_buffer[s_length] = '\0';
    if (s_length == 1) {
        db_status_led_start(DB_LED_COMMAND_ENTRY);
    }
    (void)esp_timer_stop(s_timeout_timer);  /* ESP_ERR_INVALID_STATE when not running is expected */
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_timer_start_once(s_timeout_timer, (uint64_t)CONFIG_DB_KEYPAD_INPUT_TIMEOUT_S * 1000000U));
}

void input_timeout(void)
{
    if (s_length > 0) {
        ESP_LOGI(TAG, "input cleared after %d s without a key", CONFIG_DB_KEYPAD_INPUT_TIMEOUT_S);
        clear();
    }
}
