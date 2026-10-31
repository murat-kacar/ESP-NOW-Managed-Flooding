#include "health.h"

#include <inttypes.h>
#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "registry.h"

static const char *TAG = "health";

#define APARTMENT_SLOTS 256  /* apartment numbers are 8 bits */
#define MISSED_LIMIT    3    /* intervals without a report before a module is logged as missing (ADR 0015) */

static const char *reset_reason_name(uint8_t reason)
{
    switch ((esp_reset_reason_t)reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "eFuse error";
    case ESP_RST_PWR_GLITCH: return "power glitch";
    case ESP_RST_CPU_LOCKUP: return "CPU lockup";
    default: return "unknown";
    }
}

#if CONFIG_DB_HEARTBEAT
#define INTERVAL_US ((int64_t)CONFIG_DB_HEARTBEAT_INTERVAL_S * 1000000)

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;
static int64_t s_heard_us[APARTMENT_SLOTS];  /* last report, or the start of supervision */
static bool s_missing[APARTMENT_SLOTS];

/* Runs in the esp_timer task once per interval. */
static void check_cb(void *arg)
{
    const int64_t now = esp_timer_get_time();
    for (int apartment = CONFIG_DB_APARTMENT_MIN; apartment <= CONFIG_DB_APARTMENT_MAX; apartment++) {
        registry_entry_t entry;
        if (!registry_get((uint8_t)apartment, &entry)) {
            continue;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const int64_t silent_us = now - s_heard_us[apartment];
        const bool missing = silent_us > MISSED_LIMIT * INTERVAL_US;
        s_missing[apartment] = missing;
        xSemaphoreGive(s_lock);
        if (missing) {
            ESP_LOGW(TAG, "apartment %d: no report for %" PRId64 " s", apartment, silent_us / 1000000);
        }
    }
}
#endif

esp_err_t health_init(void)
{
#if CONFIG_DB_HEARTBEAT
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "no memory");
    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < APARTMENT_SLOTS; i++) {
        s_heard_us[i] = now;  /* every registered module gets 3 intervals after boot */
    }
    const esp_timer_create_args_t args = { .callback = check_cb, .name = "health" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_timer), TAG, "timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_timer, INTERVAL_US), TAG, "timer start failed");
    ESP_LOGI(TAG, "expecting a report from every module every %d s", CONFIG_DB_HEARTBEAT_INTERVAL_S);
#endif
    return ESP_OK;
}

void health_on_report(uint8_t apartment, const db_health_t *report)
{
#if CONFIG_DB_HEARTBEAT
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool was_missing = s_missing[apartment];
    const int64_t silent_us = esp_timer_get_time() - s_heard_us[apartment];
    s_missing[apartment] = false;
    s_heard_us[apartment] = esp_timer_get_time();
    xSemaphoreGive(s_lock);
    if (was_missing) {
        ESP_LOGI(TAG, "apartment %u: reports resumed after %" PRId64 " s", apartment, silent_us / 1000000);
    }
#endif
    const bool faults = report->door_unacknowledged > 0 || report->frames_rejected > 0;
    ESP_LOG_LEVEL_LOCAL(faults ? ESP_LOG_WARN : ESP_LOG_INFO, TAG,  /* like ESP_LOGx, with a runtime level */
                        "apartment %u: up %" PRIu32 " s (last restart: %s), rings %" PRIu32 ", door requests %" PRIu32
                        " (%" PRIu32 " unacknowledged), rejected frames %" PRIu32 ", RSSI at the module %d dBm"
                        ", free heap %" PRIu32 " B (lowest %" PRIu32 " B)",
                        apartment, report->uptime_s, reset_reason_name(report->reset_reason), report->rings,
                        report->door_requests, report->door_unacknowledged, report->frames_rejected, report->last_rssi,
                        report->free_heap, report->min_free_heap);
}

void health_on_paired(uint8_t apartment)
{
#if CONFIG_DB_HEARTBEAT
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_heard_us[apartment] = esp_timer_get_time();
    s_missing[apartment] = false;
    xSemaphoreGive(s_lock);
#else
    (void)apartment;
#endif
}
