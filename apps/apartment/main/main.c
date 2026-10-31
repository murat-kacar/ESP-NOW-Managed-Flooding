#include <string.h>

#include "bell.h"
#include "buttons.h"
#include "db_net.h"
#include "db_proto.h"
#include "db_status_led.h"
#include "db_wdt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "heartbeat.h"
#include "identity.h"
#include "pairing.h"

static const char *TAG = "apartment";

#define APP_QUEUE_LENGTH 4
#define APP_TASK_STACK   4096
#define APP_TASK_PRIO    4

typedef enum { EVENT_DOOR_OPEN, EVENT_PAIRING, EVENT_RESET, EVENT_HEARTBEAT } app_event_t;

static QueueHandle_t s_events;
static SemaphoreHandle_t s_identity_lock;
static identity_t s_identity;
static bool s_paired;

/* Button callbacks run in the button timer task; blocking work happens in app_task. */
static void post_event(app_event_t event)
{
    if (xQueueSend(s_events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full");
    }
}
static void on_door_open(void) { post_event(EVENT_DOOR_OPEN); }
static void on_pairing(void) { post_event(EVENT_PAIRING); }
static void on_reset(void) { post_event(EVENT_RESET); }
static void on_heartbeat_due(void) { post_event(EVENT_HEARTBEAT); }  /* esp_timer task */

static bool get_identity(identity_t *out)
{
    xSemaphoreTake(s_identity_lock, portMAX_DELAY);
    bool paired = s_paired;
    *out = s_identity;
    xSemaphoreGive(s_identity_lock);
    return paired;
}

static void handle_pair_config(const uint8_t src[DB_MAC_LEN], const db_msg_t *msg, bool retry)
{
    identity_t current;
    if (get_identity(&current)) {
        /* The panel did not get our ACK and retried with the same counter: acknowledge again. */
        if (retry && memcmp(src, current.panel_mac, DB_MAC_LEN) == 0 && msg->apartment == current.apartment &&
                msg->session == current.session) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_ack(src, DB_OP_PAIR_CONFIG_ACK, msg));
        }
        return;
    }
    if (retry || !pairing_is_expected_panel(src)) {
        return;
    }
    identity_t identity = { .apartment = msg->apartment, .channel = msg->channel, .session = msg->session };
    memcpy(identity.panel_mac, src, DB_MAC_LEN);
    if (identity_save(&identity) != ESP_OK) {
        ESP_LOGE(TAG, "storing the identity failed");
        pairing_finish(false);
        return;
    }
    xSemaphoreTake(s_identity_lock, portMAX_DELAY);
    s_identity = identity;
    s_paired = true;
    xSemaphoreGive(s_identity_lock);

    ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_set_channel(identity.channel));
    ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_ack(src, DB_OP_PAIR_CONFIG_ACK, msg));
    pairing_finish(true);
    ESP_LOGI(TAG, "paired as apartment %u on channel %u", identity.apartment, identity.channel);

    /* SPEC 5.1: full brightness for 2 s and a bell pulse, then the paired state. */
    db_status_led_stop(DB_LED_UNPAIRED);
    db_status_led_start(DB_LED_PAIRED_IDLE);
    db_status_led_start(DB_LED_SUCCESS);
    ESP_ERROR_CHECK_WITHOUT_ABORT(bell_ring());
    heartbeat_start();
}

/* Runs in the db_proto task, after authentication and the replay check. */
static void on_message(const uint8_t src[DB_MAC_LEN], const db_msg_t *msg, const uint8_t *body, size_t body_len,
                       bool retry)
{
    (void)body;  /* no message to the module carries a body */
    (void)body_len;
    if (msg->opcode == DB_OP_PAIR_CONFIG) {
        handle_pair_config(src, msg, retry);
        return;
    }
    identity_t identity;
    if (!get_identity(&identity) || memcmp(src, identity.panel_mac, DB_MAC_LEN) != 0) {
        return;  /* only the paired panel is accepted */
    }
    if (msg->opcode != DB_OP_RING || msg->apartment != identity.apartment || msg->session != identity.session) {
        return;  /* messages of an earlier pairing session are dropped */
    }
    if (!retry) {  /* a retry is acknowledged again, the bell does not ring twice */
        ESP_LOGI(TAG, "ring");
        heartbeat_count_ring();
        ESP_ERROR_CHECK_WITHOUT_ABORT(bell_ring());
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_ack(src, DB_OP_RING_ACK, msg));
}

static void request_door_open(void)
{
    identity_t identity;
    if (!get_identity(&identity)) {
        ESP_LOGW(TAG, "door-open ignored: not paired");
        return;
    }
    db_msg_t msg = { .opcode = DB_OP_OPEN_DOOR, .apartment = identity.apartment, .session = identity.session };
    esp_err_t err = db_proto_request(identity.panel_mac, &msg, DB_OP_OPEN_DOOR_ACK, CONFIG_DB_OPEN_DOOR_RETRIES);
    heartbeat_count_door_request(err == ESP_OK);
    ESP_LOGI(TAG, "door-open request: %s", err == ESP_OK ? "acknowledged" : esp_err_to_name(err));
}

static void send_heartbeat(void)
{
    identity_t identity;
    if (get_identity(&identity)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(heartbeat_send(&identity));
    }
}

static void start_pairing(void)
{
    identity_t identity;
    if (get_identity(&identity)) {
        ESP_LOGW(TAG, "already paired as apartment %u; reset the module before pairing again", identity.apartment);
        return;
    }
    esp_err_t err = pairing_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pairing not started: %s", esp_err_to_name(err));
    }
}

static void factory_reset(void)
{
    ESP_LOGI(TAG, "module reset: erasing identity, key and peer counters");
    pairing_finish(false);
    ESP_ERROR_CHECK_WITHOUT_ABORT(identity_erase());
    ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_erase_key());
    ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_erase_peer_counters());
    esp_restart();  /* starts again unpaired with a clean espnow_sec state */
}

static void app_task(void *arg)
{
    ESP_ERROR_CHECK(db_wdt_subscribe());  /* a stuck application task restarts the module (ADR 0016) */
    app_event_t event;
    for (;;) {
        db_wdt_feed();
        if (xQueueReceive(s_events, &event, DB_WDT_FEED_TICKS) != pdTRUE) {
            continue;
        }
        switch (event) {
        case EVENT_DOOR_OPEN:
            request_door_open();
            break;
        case EVENT_PAIRING:
            start_pairing();
            break;
        case EVENT_RESET:
            factory_reset();
            break;
        case EVENT_HEARTBEAT:
            send_heartbeat();
            break;
        }
    }
}

void app_main(void)
{
    s_events = xQueueCreate(APP_QUEUE_LENGTH, sizeof(app_event_t));
    s_identity_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_events && s_identity_lock ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(db_net_init());
    ESP_ERROR_CHECK(db_status_led_init(CONFIG_DB_STATUS_LED_GPIO));
    ESP_ERROR_CHECK(bell_init());

    s_paired = identity_load(&s_identity);
    if (s_paired && !db_net_has_key()) {
        ESP_LOGW(TAG, "identity without a key: treating the module as unpaired");
        ESP_ERROR_CHECK_WITHOUT_ABORT(identity_erase());
        s_paired = false;
    }
    if (!s_paired && db_net_has_key()) {
        /* Left by a pairing window cut short by a power loss: an unpaired module holds no key. */
        ESP_LOGW(TAG, "key without an identity: erasing the key");
        ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_erase_key());
    }
    if (s_paired) {
        ESP_ERROR_CHECK(db_net_set_channel(s_identity.channel));
        db_status_led_start(DB_LED_PAIRED_IDLE);
        ESP_LOGI(TAG, "apartment %u, panel " MACSTR ", channel %u", s_identity.apartment,
                 MAC2STR(s_identity.panel_mac), s_identity.channel);
    } else {
        db_status_led_start(DB_LED_UNPAIRED);
        ESP_LOGI(TAG, "not paired");
    }

    ESP_ERROR_CHECK(pairing_init());
    ESP_ERROR_CHECK(heartbeat_init(on_heartbeat_due));
    ESP_ERROR_CHECK(db_proto_init(on_message, pairing_on_hello));
    ESP_ERROR_CHECK(xTaskCreate(app_task, "apartment", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);

    const buttons_handlers_t handlers = {
        .door_open = on_door_open,
        .pairing = on_pairing,
        .reset = on_reset,
    };
    ESP_ERROR_CHECK(buttons_init(&handlers));
    if (s_paired) {
        heartbeat_start();
    }
    ESP_LOGI(TAG, "started");
}
