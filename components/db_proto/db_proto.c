#include "db_proto.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "espnow.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "db_proto";

#define NS_OWN_COUNTER   "db_ctr_own"   /* this device's send counter reservation */
#define NS_PEER_COUNTER  "db_ctr_peer"  /* last accepted counter per peer MAC */
#define KEY_RESERVED     "reserved"
#define RX_QUEUE_LENGTH  8
#define TASK_STACK_SIZE  4096
#define TASK_PRIORITY    5
#define SEND_TIMEOUT_MS  1000  /* maximum time espnow_send() may block */

typedef struct {
    uint8_t src[DB_MAC_LEN];
    db_msg_t msg;
} rx_item_t;

typedef struct {
    bool active;
    uint8_t dest[DB_MAC_LEN];
    uint8_t ack_opcode;
    uint32_t session;
    uint32_t counter;
} pending_request_t;

static db_proto_handler_t s_handler;
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_lock;          /* counters and pending request */
static SemaphoreHandle_t s_request_lock;  /* one request at a time */
static SemaphoreHandle_t s_ack;
static uint32_t s_counter;                /* last counter used */
static uint32_t s_reserved;               /* counters up to this value are covered by NVS */
static pending_request_t s_pending;

static const espnow_frame_head_t s_frame = {
    .channel = ESPNOW_CHANNEL_CURRENT,
#if CONFIG_DB_FILTER_WEAK_SIGNAL
    .filter_weak_signal = true,
#endif
    .security = true,
    .broadcast = true,  /* only broadcast frames are forwarded (ADR 0001); dest_addr selects the receiver */
    .retransmit_count = CONFIG_DB_RETRANSMIT_COUNT,
    .forward_ttl = CONFIG_DB_FORWARD_TTL,
    .forward_rssi = CONFIG_DB_FORWARD_RSSI,
};

static bool is_ack(uint8_t opcode)
{
    return opcode == DB_OP_RING_ACK || opcode == DB_OP_OPEN_DOOR_ACK || opcode == DB_OP_PAIR_CONFIG_ACK;
}

static void mac_to_key(const uint8_t mac[DB_MAC_LEN], char key[13])
{
    snprintf(key, 13, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static esp_err_t nvs_write_u32(const char *ns, const char *key, uint32_t value)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(ns, NVS_READWRITE, &nvs), TAG, "nvs_open %s failed", ns);
    esp_err_t err = nvs_set_u32(nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static uint32_t nvs_read_u32(const char *ns, const char *key)
{
    nvs_handle_t nvs;
    uint32_t value = 0;
    if (nvs_open(ns, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u32(nvs, key, &value);  /* missing key leaves 0 */
        nvs_close(nvs);
    }
    return value;
}

static esp_err_t nvs_erase_namespace(const char *ns)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(ns, NVS_READWRITE, &nvs), TAG, "nvs_open %s failed", ns);
    esp_err_t err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

/* Block reservation (ADR 0002): NVS is written once per CONFIG_DB_REPLAY_COUNTER_BLOCK messages.
 * After a reboot counting resumes above the reserved block, so a counter is never reused. */
static esp_err_t next_counter(uint32_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (s_counter >= s_reserved) {
        uint32_t reserved = s_counter + CONFIG_DB_REPLAY_COUNTER_BLOCK;
        err = nvs_write_u32(NS_OWN_COUNTER, KEY_RESERVED, reserved);
        if (err == ESP_OK) {
            s_reserved = reserved;
        }
    }
    if (err == ESP_OK) {
        *out = ++s_counter;
    }
    xSemaphoreGive(s_lock);
    return err;
}

static esp_err_t raw_send(const uint8_t dest[DB_MAC_LEN], const db_msg_t *msg)
{
    return espnow_send(ESPNOW_DATA_TYPE_DATA, dest, msg, sizeof(*msg), &s_frame, pdMS_TO_TICKS(SEND_TIMEOUT_MS));
}

/* Runs in the esp-now task: copy and hand over to the protocol task (SPEC 7.1). */
static esp_err_t on_espnow_data(uint8_t *src_addr, void *data, size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    if (size != sizeof(db_msg_t)) {
        return ESP_OK;
    }
    rx_item_t item;
    memcpy(item.src, src_addr, DB_MAC_LEN);
    memcpy(&item.msg, data, sizeof(item.msg));
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "receive queue full, message dropped");
    }
    return ESP_OK;
}

static void handle_ack(const rx_item_t *item)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool expected = s_pending.active && s_pending.ack_opcode == item->msg.opcode &&
                    s_pending.session == item->msg.session && s_pending.counter == item->msg.ack_counter &&
                    memcmp(s_pending.dest, item->src, DB_MAC_LEN) == 0;
    xSemaphoreGive(s_lock);
    if (expected && db_proto_check_counter(item->src, item->msg.counter) == DB_REPLAY_NEW) {
        xSemaphoreGive(s_ack);
    }
}

static void protocol_task(void *arg)
{
    rx_item_t item;
    for (;;) {
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item.msg.version != DB_PROTO_VERSION) {
            ESP_LOGW(TAG, "message version %u ignored", item.msg.version);
            continue;
        }
        if (is_ack(item.msg.opcode)) {
            handle_ack(&item);
        } else {
            s_handler(item.src, &item.msg);
        }
    }
}

esp_err_t db_proto_init(db_proto_handler_t handler)
{
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, TAG, "handler is required");
    ESP_RETURN_ON_FALSE(s_rx_queue == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    s_handler = handler;
    s_lock = xSemaphoreCreateMutex();
    s_request_lock = xSemaphoreCreateMutex();
    s_ack = xSemaphoreCreateBinary();
    s_rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(rx_item_t));
    ESP_RETURN_ON_FALSE(s_lock && s_request_lock && s_ack && s_rx_queue, ESP_ERR_NO_MEM, TAG, "no memory");

    s_reserved = nvs_read_u32(NS_OWN_COUNTER, KEY_RESERVED);
    s_counter = s_reserved;
    ESP_LOGI(TAG, "send counter resumes at %" PRIu32, s_counter + 1);

    ESP_RETURN_ON_FALSE(xTaskCreate(protocol_task, "db_proto", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create failed");
    return espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, on_espnow_data);
}

esp_err_t db_proto_send(const uint8_t dest[DB_MAC_LEN], db_msg_t *msg)
{
    uint32_t counter;  /* db_msg_t is packed: no pointers to its members */
    ESP_RETURN_ON_ERROR(next_counter(&counter), TAG, "counter reservation failed");
    msg->version = DB_PROTO_VERSION;
    msg->counter = counter;
    return raw_send(dest, msg);
}

esp_err_t db_proto_ack(const uint8_t dest[DB_MAC_LEN], db_opcode_t ack_opcode, const db_msg_t *request)
{
    db_msg_t ack = {
        .opcode = ack_opcode,
        .apartment = request->apartment,
        .session = request->session,
        .ack_counter = request->counter,
    };
    return db_proto_send(dest, &ack);
}

esp_err_t db_proto_request(const uint8_t dest[DB_MAC_LEN], db_msg_t *msg, db_opcode_t ack_opcode, int retries)
{
    xSemaphoreTake(s_request_lock, portMAX_DELAY);
    uint32_t counter;  /* db_msg_t is packed: no pointers to its members */
    esp_err_t err = next_counter(&counter);
    if (err == ESP_OK) {
        msg->version = DB_PROTO_VERSION;
        msg->counter = counter;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_pending = (pending_request_t) {
            .active = true, .ack_opcode = ack_opcode, .session = msg->session, .counter = counter,
        };
        memcpy(s_pending.dest, dest, DB_MAC_LEN);
        xSemaphoreGive(s_lock);
        xSemaphoreTake(s_ack, 0);  /* drop a stale signal */

        err = ESP_ERR_TIMEOUT;
        for (int attempt = 0; attempt <= retries && err != ESP_OK; attempt++) {
            esp_err_t sent = raw_send(dest, msg);  /* same counter on every attempt */
            if (sent != ESP_OK) {
                ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(sent));
            }
            if (xSemaphoreTake(s_ack, pdMS_TO_TICKS(CONFIG_DB_ACK_TIMEOUT_MS)) == pdTRUE) {
                err = ESP_OK;
            }
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_pending.active = false;
        xSemaphoreGive(s_lock);
    }
    xSemaphoreGive(s_request_lock);
    return err;
}

db_replay_t db_proto_check_counter(const uint8_t src[DB_MAC_LEN], uint32_t counter)
{
    char key[13];
    mac_to_key(src, key);
    uint32_t last = nvs_read_u32(NS_PEER_COUNTER, key);
    if (counter < last) {
        return DB_REPLAY_OLD;
    }
    if (counter == last) {
        return DB_REPLAY_RETRY;
    }
    esp_err_t err = nvs_write_u32(NS_PEER_COUNTER, key, counter);
    if (err != ESP_OK) {
        /* Without a stored counter the message could be replayed later, so it is not accepted. */
        ESP_LOGE(TAG, "storing counter failed: %s", esp_err_to_name(err));
        return DB_REPLAY_OLD;
    }
    return DB_REPLAY_NEW;
}

esp_err_t db_proto_forget_peer(const uint8_t mac[DB_MAC_LEN])
{
    char key[13];
    mac_to_key(mac, key);
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS_PEER_COUNTER, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    esp_err_t err = nvs_erase_key(nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t db_proto_erase_peer_counters(void)
{
    return nvs_erase_namespace(NS_PEER_COUNTER);
}

esp_err_t db_proto_erase_all_counters(void)
{
    ESP_RETURN_ON_ERROR(nvs_erase_namespace(NS_PEER_COUNTER), TAG, "erase peer counters failed");
    ESP_RETURN_ON_ERROR(nvs_erase_namespace(NS_OWN_COUNTER), TAG, "erase own counter failed");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_counter = 0;
    s_reserved = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
