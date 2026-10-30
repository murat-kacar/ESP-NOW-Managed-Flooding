#pragma once

#include <stdint.h>

#include "esp_err.h"

#define INPUT_MAX_CODE 16

typedef enum {
    COMMAND_RING,            /**< <apartment> # */
    COMMAND_OPEN_DOOR,       /**< * <user_code> # */
    COMMAND_PAIR,            /**< * <admin> * 1 * <apartment> # */
    COMMAND_DELETE,          /**< * <admin> * 2 * <apartment> * <apartment> # */
    COMMAND_SET_USER_CODE,   /**< * <admin> * 3 * <new> * <new> # */
    COMMAND_SET_ADMIN_CODE,  /**< * <admin> * 4 * <new> * <new> # */
    COMMAND_BUILDING_RESET,  /**< * <admin> * 6 * <admin> # */
} command_type_t;

typedef struct {
    command_type_t type;
    uint8_t apartment;
    char code[INPUT_MAX_CODE + 1];
} command_t;

typedef void (*command_handler_t)(const command_t *command);

/**
 * @brief Keypad input state (SPEC 3, 4). Call input_key() and input_timeout() from one task.
 *
 * @param handler      called for each valid command
 * @param post_timeout called from a timer when CONFIG_DB_KEYPAD_INPUT_TIMEOUT_S passes without a key;
 *                     it should make the input task call input_timeout()
 */
esp_err_t input_init(command_handler_t handler, void (*post_timeout)(void));

void input_key(char key);

/** @brief Clear unfinished input after the inactivity timeout. */
void input_timeout(void);
