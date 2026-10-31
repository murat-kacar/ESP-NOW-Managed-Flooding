#include "heartbeat.h"

#include <stdatomic.h>
#include <stdint.h>

#include "db_proto.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "heartbeat";

static atomic_uint s_rings;
static atomic_uint s_door_requests;
static atomic_uint s_door_unacknowledged;

#if CONFIG_DB_HEARTBEAT
#define INTERVAL_US ((uint64_t)CONFIG_DB_HEARTBEAT_INTERVAL_S * 1000000U)

static esp_timer_handle_t s_timer;
static void (*s_due)(void);

/* Interval with +/-10 % jitter, so modules started together do not stay in step (ADR 0015). */
static uint64_t next_delay_us(void)
{
    return INTERVAL_US - INTERVAL_US / 10 + (uint64_t)esp_random() % (INTERVAL_US / 5 + 1);
}

static void timer_cb(void *arg)
{
    s_due();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(s_timer, next_delay_us()));
}
#endif

esp_err_t heartbeat_init(void (*due)(void))
{
#if CONFIG_DB_HEARTBEAT
    ESP_RETURN_ON_FALSE(due != NULL, ESP_ERR_INVALID_ARG, TAG, "callback is required");
    s_due = due;
    const esp_timer_create_args_t args = { .callback = timer_cb, .name = "heartbeat" };
    return esp_timer_create(&args, &s_timer);
#else
    (void)due;
    return ESP_OK;
#endif
}

void heartbeat_start(void)
{
#if CONFIG_DB_HEARTBEAT
    if (esp_timer_is_active(s_timer)) {
        return;
    }
    /* First report at a random point of the interval. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(s_timer, (uint64_t)esp_random() % INTERVAL_US + 1));
    ESP_LOGI(TAG, "reporting to the panel every %d s", CONFIG_DB_HEARTBEAT_INTERVAL_S);
#endif
}

esp_err_t heartbeat_send(const identity_t *identity)
{
    db_proto_stats_t stats;
    db_proto_get_stats(&stats);
    const db_health_t health = {
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000),
        .rings = atomic_load(&s_rings),
        .door_requests = atomic_load(&s_door_requests),
        .door_unacknowledged = atomic_load(&s_door_unacknowledged),
        .frames_rejected = stats.frames_rejected,
        .free_heap = esp_get_free_heap_size(),
        .min_free_heap = esp_get_minimum_free_heap_size(),
        .reset_reason = (uint8_t)esp_reset_reason(),
        .last_rssi = stats.last_rssi,
    };
    db_msg_t msg = { .opcode = DB_OP_HEARTBEAT, .apartment = identity->apartment, .session = identity->session };
    esp_err_t err = db_proto_send_body(identity->panel_mac, &msg, &health, sizeof(health));
    ESP_LOGD(TAG, "report sent: %s", esp_err_to_name(err));
    return err;
}

void heartbeat_count_ring(void)
{
    atomic_fetch_add(&s_rings, 1);
}

void heartbeat_count_door_request(bool acknowledged)
{
    atomic_fetch_add(&s_door_requests, 1);
    if (!acknowledged) {
        atomic_fetch_add(&s_door_unacknowledged, 1);
    }
}
