#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#if CONFIG_ESP_TASK_WDT_EN
#include "esp_task_wdt.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Task watchdog supervision (ADR 0016). A supervised task subscribes once, then feeds the watchdog at least once per
 * DB_WDT_FEED_TICKS: it waits for work with that timeout and feeds on every loop pass. A task that stops feeding
 * for CONFIG_ESP_TASK_WDT_TIMEOUT_S makes the task watchdog restart the device (CONFIG_ESP_TASK_WDT_PANIC).
 */

#if CONFIG_ESP_TASK_WDT_EN

/** Longest time a supervised task may block between two feeds: a third of the watchdog timeout. */
#define DB_WDT_FEED_TICKS pdMS_TO_TICKS(CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000 / 3)

/** @brief Subscribe the calling task to the task watchdog. */
static inline esp_err_t db_wdt_subscribe(void)
{
    return esp_task_wdt_add(NULL);
}

/** @brief Feed the watchdog for the calling task; does nothing for a task that is not subscribed. */
static inline void db_wdt_feed(void)
{
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_task_wdt_reset());
    }
}

#else

#define DB_WDT_FEED_TICKS portMAX_DELAY

static inline esp_err_t db_wdt_subscribe(void)
{
    return ESP_OK;
}

static inline void db_wdt_feed(void)
{
}

#endif

#ifdef __cplusplus
}
#endif
