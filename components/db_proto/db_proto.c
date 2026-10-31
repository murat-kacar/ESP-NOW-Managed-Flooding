#include "db_proto.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "db_nvs.h"
#include "db_wdt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_security.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"

static const char *TAG = "db_proto";

#define NS_OWN_COUNTER   "db_ctr_own"   /* this device's send counter reservation */
#define NS_PEER_COUNTER  "db_ctr_peer"  /* last accepted command counter per peer MAC */
#define KEY_RESERVED     "reserved"
#define PEER_KEY_SIZE    13             /* 12 hex digits of the MAC and the terminator */
#define PEER_CACHE_SIZE  256            /* newest counter seen per peer, in RAM: one panel, or up to 254 modules */
#define RX_QUEUE_LENGTH  8
#define TASK_STACK_SIZE  4096
#define TASK_PRIORITY    5
#define SEND_TIMEOUT_MS  1000  /* maximum time espnow_send() may block */

#define AUTH_LABEL       "doorbell message auth"
#define AUTH_KEY_LEN     32
#define AUTH_ALG         PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), DB_TAG_LEN)

/* Authenticated frame on air: message, opcode-specific body, tag. The frame size gives the body length. */
#define FRAME_MIN        (sizeof(db_msg_t) + DB_TAG_LEN)
#define FRAME_MAX        (FRAME_MIN + DB_BODY_MAX)
#define AUTH_INPUT_MAX   (2 * DB_MAC_LEN + sizeof(db_msg_t) + DB_BODY_MAX)

typedef struct {
    uint8_t bytes[FRAME_MAX];
    size_t len;
} frame_t;

/* Pairing discovery frame on air: plaintext, carries nothing secret. */
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t opcode;
} hello_frame_t;

/* The wire format (SPEC 7.2) is the in-memory layout: fixed sizes, little-endian fields. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the wire format is little-endian");
_Static_assert(sizeof(db_msg_t) == 16, "db_msg_t is 16 bytes on air");
_Static_assert(sizeof(db_health_t) == 30, "db_health_t is 30 bytes on air");
_Static_assert(sizeof(db_health_t) <= DB_BODY_MAX, "the heartbeat body must fit DB_BODY_MAX");
_Static_assert(sizeof(hello_frame_t) == 2, "a discovery frame is 2 bytes on air");
_Static_assert(sizeof(hello_frame_t) < FRAME_MIN, "discovery and authenticated frames differ in size");
_Static_assert(FRAME_MAX <= ESPNOW_DATA_LEN, "the largest frame must fit one encrypted esp-now packet");
_Static_assert(DB_MAC_LEN == ESPNOW_ADDR_LEN, "DB_MAC_LEN must match esp-now");
#if CONFIG_ESP_TASK_WDT_EN
/* db_proto_request() feeds the caller's watchdog once per attempt (ADR 0016). */
_Static_assert(SEND_TIMEOUT_MS + CONFIG_DB_ACK_TIMEOUT_MS < CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
               "one request attempt must be shorter than the task watchdog timeout");
#endif

/* Result of the replay check (ADR 0002, ADR 0015). */
typedef enum {
    REPLAY_NEW,    /* newer than every counter seen from this peer */
    REPLAY_RETRY,  /* a command whose counter equals the last accepted one: a retry */
    REPLAY_OLD,    /* not newer, or the new counter could not be stored: drop */
} replay_t;

typedef struct {
    uint8_t src[DB_MAC_LEN];
    int8_t rssi;  /* of the received copy, i.e. of the last hop when the frame was forwarded */
    size_t size;
    union {
        uint8_t frame[FRAME_MAX];
        hello_frame_t hello;
    } data;
} rx_item_t;

typedef struct {
    bool active;
    uint8_t dest[DB_MAC_LEN];
    uint8_t ack_opcode;
    uint32_t session;
    uint32_t counter;
} pending_request_t;

typedef struct {
    bool used;
    uint8_t mac[DB_MAC_LEN];
    uint32_t counter;
} peer_seen_t;

static db_proto_handler_t s_handler;
static db_proto_hello_handler_t s_hello_handler;
static uint8_t s_own_mac[DB_MAC_LEN];
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_lock;          /* counters, peer cache, statistics and pending request */
static SemaphoreHandle_t s_request_lock;  /* one request at a time */
static SemaphoreHandle_t s_ack;
static uint32_t s_counter;                /* last counter used */
static uint32_t s_reserved;               /* counters up to this value are covered by NVS */
static pending_request_t s_pending;
static peer_seen_t s_seen[PEER_CACHE_SIZE];
static size_t s_seen_next;                /* entry replaced next when the cache is full */
static db_proto_stats_t s_stats;

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

/* Discovery happens next to the panel (ADR 0007): plaintext and not forwarded. */
static const espnow_frame_head_t s_hello_frame = {
    .channel = ESPNOW_CHANNEL_CURRENT,
    .broadcast = true,
    .retransmit_count = CONFIG_DB_RETRANSMIT_COUNT,
    .forward_ttl = 0,
};

/* Import a 32-byte HMAC-SHA256 key for the given algorithm. */
static esp_err_t import_hmac_key(const uint8_t *key, psa_algorithm_t alg, psa_key_usage_t usage,
                                 mbedtls_svc_key_id_t *id)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, AUTH_KEY_LEN * 8);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    return psa_import_key(&attributes, key, AUTH_KEY_LEN, id) == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

/* K_auth = HKDF-Expand(app key, AUTH_LABEL, 32) = HMAC-SHA256(app key, AUTH_LABEL || 0x01) (RFC 5869, one block). */
static esp_err_t derive_auth_key(mbedtls_svc_key_id_t *auth_key)
{
    uint8_t app_key[APP_KEY_LEN];
    if (espnow_get_key(app_key) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;  /* not paired yet */
    }
    _Static_assert(APP_KEY_LEN == AUTH_KEY_LEN, "the esp-now application key is used as the HKDF PRK");
    mbedtls_svc_key_id_t prk = 0;
    esp_err_t err = import_hmac_key(app_key, PSA_ALG_HMAC(PSA_ALG_SHA_256), PSA_KEY_USAGE_SIGN_MESSAGE, &prk);
    memset(app_key, 0, sizeof(app_key));
    if (err != ESP_OK) {
        return err;
    }
    static const uint8_t info[] = AUTH_LABEL "\x01";
    uint8_t okm[AUTH_KEY_LEN];
    size_t okm_len = 0;
    psa_status_t status = psa_mac_compute(prk, PSA_ALG_HMAC(PSA_ALG_SHA_256), info, sizeof(info) - 1,
                                          okm, sizeof(okm), &okm_len);
    psa_destroy_key(prk);
    if (status != PSA_SUCCESS || okm_len != sizeof(okm)) {
        return ESP_FAIL;
    }
    err = import_hmac_key(okm, AUTH_ALG, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE, auth_key);
    memset(okm, 0, sizeof(okm));
    return err;
}

/* MAC input: src MAC || dest MAC || message || body. */
static size_t auth_input(const uint8_t src[DB_MAC_LEN], const uint8_t dest[DB_MAC_LEN], const uint8_t *payload,
                         size_t payload_len, uint8_t out[AUTH_INPUT_MAX])
{
    memcpy(out, src, DB_MAC_LEN);
    memcpy(out + DB_MAC_LEN, dest, DB_MAC_LEN);
    memcpy(out + 2 * DB_MAC_LEN, payload, payload_len);
    return 2 * DB_MAC_LEN + payload_len;
}

/* Build message || body || tag. */
static esp_err_t build_frame(const uint8_t dest[DB_MAC_LEN], const db_msg_t *msg, const void *body, size_t body_len,
                             frame_t *frame)
{
    const size_t payload_len = sizeof(*msg) + body_len;
    memcpy(frame->bytes, msg, sizeof(*msg));
    if (body_len > 0) {
        memcpy(frame->bytes + sizeof(*msg), body, body_len);
    }
    frame->len = payload_len + DB_TAG_LEN;

    mbedtls_svc_key_id_t key = 0;
    ESP_RETURN_ON_ERROR(derive_auth_key(&key), TAG, "no authentication key");
    uint8_t input[AUTH_INPUT_MAX];
    size_t input_len = auth_input(s_own_mac, dest, frame->bytes, payload_len, input);
    size_t tag_len = 0;
    psa_status_t status = psa_mac_compute(key, AUTH_ALG, input, input_len, frame->bytes + payload_len, DB_TAG_LEN,
                                          &tag_len);
    psa_destroy_key(key);
    return status == PSA_SUCCESS && tag_len == DB_TAG_LEN ? ESP_OK : ESP_FAIL;
}

static bool verify(const uint8_t src[DB_MAC_LEN], const uint8_t *frame, size_t len)
{
    mbedtls_svc_key_id_t key = 0;
    if (derive_auth_key(&key) != ESP_OK) {
        return false;
    }
    const size_t payload_len = len - DB_TAG_LEN;
    uint8_t input[AUTH_INPUT_MAX];
    size_t input_len = auth_input(src, s_own_mac, frame, payload_len, input);
    psa_status_t status = psa_mac_verify(key, AUTH_ALG, input, input_len, frame + payload_len, DB_TAG_LEN);
    psa_destroy_key(key);
    return status == PSA_SUCCESS;  /* psa_mac_verify compares in constant time */
}

static bool is_ack(uint8_t opcode)
{
    return opcode == DB_OP_RING_ACK || opcode == DB_OP_OPEN_DOOR_ACK || opcode == DB_OP_PAIR_CONFIG_ACK;
}

static bool is_discovery(uint8_t opcode)
{
    return opcode == DB_OP_PAIR_HELLO || opcode == DB_OP_PAIR_HELLO_ACK;
}

/* Reports change no state, so their replay check stays in RAM (ADR 0015). */
static bool is_report(uint8_t opcode)
{
    return opcode == DB_OP_HEARTBEAT;
}

static void peer_key(const uint8_t mac[DB_MAC_LEN], char key[PEER_KEY_SIZE])
{
    snprintf(key, PEER_KEY_SIZE, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Newest counter seen from a peer in RAM; 0 if unknown. Caller holds s_lock. */
static peer_seen_t *seen_find(const uint8_t mac[DB_MAC_LEN])
{
    for (size_t i = 0; i < PEER_CACHE_SIZE; i++) {
        if (s_seen[i].used && memcmp(s_seen[i].mac, mac, DB_MAC_LEN) == 0) {
            return &s_seen[i];
        }
    }
    return NULL;
}

static uint32_t seen_get(const uint8_t mac[DB_MAC_LEN])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const peer_seen_t *entry = seen_find(mac);
    uint32_t counter = entry ? entry->counter : 0;
    xSemaphoreGive(s_lock);
    return counter;
}

static void seen_set(const uint8_t mac[DB_MAC_LEN], uint32_t counter)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    peer_seen_t *entry = seen_find(mac);
    if (entry == NULL) {
        for (size_t i = 0; i < PEER_CACHE_SIZE && entry == NULL; i++) {
            entry = s_seen[i].used ? NULL : &s_seen[i];
        }
    }
    if (entry == NULL) {
        /* Full: replace in turn. A replaced peer falls back to its NVS counter, which still blocks replayed
         * commands; only an old report of that peer could be shown again. */
        entry = &s_seen[s_seen_next];
        s_seen_next = (s_seen_next + 1) % PEER_CACHE_SIZE;
    }
    *entry = (peer_seen_t) { .used = true, .counter = counter };
    memcpy(entry->mac, mac, DB_MAC_LEN);
    xSemaphoreGive(s_lock);
}

static void seen_clear(const uint8_t *mac)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (mac == NULL) {
        memset(s_seen, 0, sizeof(s_seen));
    } else {
        peer_seen_t *entry = seen_find(mac);
        if (entry != NULL) {
            entry->used = false;
        }
    }
    xSemaphoreGive(s_lock);
}

/* Block reservation (ADR 0002): NVS is written once per CONFIG_DB_REPLAY_COUNTER_BLOCK messages.
 * After a reboot counting resumes above the reserved block, so a counter is never reused. */
static esp_err_t next_counter(uint32_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (s_counter >= s_reserved) {
        uint32_t reserved = s_counter + CONFIG_DB_REPLAY_COUNTER_BLOCK;
        err = db_nvs_set_u32(NS_OWN_COUNTER, KEY_RESERVED, reserved);
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

/*
 * Replay check of an authenticated frame (ADR 0002, ADR 0015). Runs in the protocol task only.
 * Commands and ACKs are checked against the counter in NVS and update it; reports are checked in RAM only.
 * Both must also be newer than the newest counter seen in RAM.
 */
static replay_t check_counter(const uint8_t src[DB_MAC_LEN], uint32_t counter, bool persistent)
{
    char key[PEER_KEY_SIZE];
    peer_key(src, key);
    uint32_t stored = 0;
    esp_err_t err = db_nvs_get_u32(NS_PEER_COUNTER, key, &stored);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "reading the counter of " MACSTR " failed: %s", MAC2STR(src), esp_err_to_name(err));
        return REPLAY_OLD;
    }
    if (persistent && counter == stored) {
        return REPLAY_RETRY;
    }
    if (counter <= stored || counter <= seen_get(src)) {
        return REPLAY_OLD;
    }
    if (persistent) {
        err = db_nvs_set_u32(NS_PEER_COUNTER, key, counter);
        if (err != ESP_OK) {
            /* Without a stored counter the command could be replayed later, so it is not accepted. */
            ESP_LOGE(TAG, "storing the counter of " MACSTR " failed: %s", MAC2STR(src), esp_err_to_name(err));
            return REPLAY_OLD;
        }
    }
    seen_set(src, counter);
    return REPLAY_NEW;
}

static void count_rejected(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stats.frames_rejected++;
    xSemaphoreGive(s_lock);
}

static esp_err_t raw_send(const uint8_t dest[DB_MAC_LEN], const frame_t *frame)
{
    return espnow_send(ESPNOW_DATA_TYPE_DATA, dest, frame->bytes, frame->len, &s_frame,
                       pdMS_TO_TICKS(SEND_TIMEOUT_MS));
}

/* Runs in the esp-now task: copy and hand over to the protocol task (SPEC 7.1). */
static esp_err_t on_espnow_data(uint8_t *src_addr, void *data, size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    if (size != sizeof(hello_frame_t) && (size < FRAME_MIN || size > FRAME_MAX)) {
        return ESP_OK;
    }
    rx_item_t item = { .size = size, .rssi = (int8_t)rx_ctrl->rssi };
    memcpy(item.src, src_addr, DB_MAC_LEN);
    memcpy(&item.data, data, size);
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "receive queue full, message dropped");
    }
    return ESP_OK;
}

static void handle_ack(const uint8_t src[DB_MAC_LEN], const db_msg_t *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool expected = s_pending.active && s_pending.ack_opcode == msg->opcode &&
                    s_pending.session == msg->session && s_pending.counter == msg->ack_counter &&
                    memcmp(s_pending.dest, src, DB_MAC_LEN) == 0;
    xSemaphoreGive(s_lock);
    if (expected) {
        xSemaphoreGive(s_ack);
    }
}

static void handle_hello(const uint8_t src[DB_MAC_LEN], int rssi, const hello_frame_t *hello)
{
    if (hello->version == DB_PROTO_VERSION && s_hello_handler && is_discovery(hello->opcode)) {
        ESP_LOGI(TAG, "discovery opcode %u from " MACSTR ", RSSI %d dBm", hello->opcode, MAC2STR(src), rssi);
        s_hello_handler(src, (db_opcode_t)hello->opcode);
    }
}

/* Authentication, version and replay checks, then dispatch (ADR 0012). */
static void handle_message(const uint8_t src[DB_MAC_LEN], int rssi, const uint8_t *frame, size_t len)
{
    if (!verify(src, frame, len)) {
        ESP_LOGW(TAG, "message without a valid authentication tag dropped");
        count_rejected();
        return;
    }
    db_msg_t msg;  /* copied out of the byte buffer: the frame has no alignment guarantee */
    memcpy(&msg, frame, sizeof(msg));
    const uint8_t *body = len > FRAME_MIN ? frame + sizeof(msg) : NULL;
    const size_t body_len = len - FRAME_MIN;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stats.last_rssi = (int8_t)rssi;
    xSemaphoreGive(s_lock);
    /* Link measurement (SPEC 9): logged only for authentic frames, so foreign traffic adds no lines. */
    ESP_LOGI(TAG, "opcode %u from " MACSTR ", counter %" PRIu32 ", RSSI %d dBm (last hop)", msg.opcode, MAC2STR(src),
             (uint32_t)msg.counter, rssi);
    if (msg.version != DB_PROTO_VERSION) {
        ESP_LOGW(TAG, "message version %u ignored", msg.version);
        count_rejected();
        return;
    }
    if (is_discovery(msg.opcode)) {
        return;  /* discovery opcodes are only valid in plaintext discovery frames */
    }
    replay_t replay = check_counter(src, msg.counter, !is_report(msg.opcode));
    if (replay == REPLAY_OLD) {
        ESP_LOGW(TAG, "old counter %" PRIu32 " from " MACSTR " dropped", (uint32_t)msg.counter, MAC2STR(src));
        count_rejected();
        return;
    }
    if (is_ack(msg.opcode)) {
        if (replay == REPLAY_NEW) {  /* a repeated ACK carries nothing new */
            handle_ack(src, &msg);
        }
        return;
    }
    s_handler(src, &msg, body, body_len, replay == REPLAY_RETRY);
}

static void protocol_task(void *arg)
{
    ESP_ERROR_CHECK(db_wdt_subscribe());  /* a stuck protocol task restarts the device (ADR 0016) */
    rx_item_t item;
    for (;;) {
        db_wdt_feed();
        if (xQueueReceive(s_rx_queue, &item, DB_WDT_FEED_TICKS) != pdTRUE) {
            continue;
        }
        if (item.size == sizeof(hello_frame_t)) {
            handle_hello(item.src, item.rssi, &item.data.hello);
        } else {
            handle_message(item.src, item.rssi, item.data.frame, item.size);
        }
    }
}

esp_err_t db_proto_init(db_proto_handler_t handler, db_proto_hello_handler_t hello_handler)
{
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, TAG, "handler is required");
    ESP_RETURN_ON_FALSE(s_rx_queue == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_FALSE(psa_crypto_init() == PSA_SUCCESS, ESP_FAIL, TAG, "psa_crypto_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, s_own_mac), TAG, "reading the MAC failed");

    esp_err_t err = db_nvs_get_u32(NS_OWN_COUNTER, KEY_RESERVED, &s_reserved);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_reserved = 0;  /* never sent: start at 1 */
    } else {
        ESP_RETURN_ON_ERROR(err, TAG, "reading the send counter failed");
    }
    s_counter = s_reserved;
    ESP_LOGI(TAG, "send counter resumes at %" PRIu32, s_counter + 1);

    s_handler = handler;
    s_hello_handler = hello_handler;
    s_lock = xSemaphoreCreateMutex();
    s_request_lock = xSemaphoreCreateMutex();
    s_ack = xSemaphoreCreateBinary();
    s_rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(rx_item_t));
    ESP_RETURN_ON_FALSE(s_lock && s_request_lock && s_ack && s_rx_queue, ESP_ERR_NO_MEM, TAG, "no memory");
    ESP_RETURN_ON_FALSE(xTaskCreate(protocol_task, "db_proto", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create failed");
    return espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, on_espnow_data);
}

esp_err_t db_proto_send_body(const uint8_t dest[DB_MAC_LEN], db_msg_t *msg, const void *body, size_t body_len)
{
    ESP_RETURN_ON_FALSE(body_len <= DB_BODY_MAX && (body_len == 0 || body != NULL), ESP_ERR_INVALID_ARG, TAG,
                        "invalid body");
    uint32_t counter;  /* db_msg_t is packed: no pointers to its members */
    ESP_RETURN_ON_ERROR(next_counter(&counter), TAG, "counter reservation failed");
    msg->version = DB_PROTO_VERSION;
    msg->counter = counter;
    frame_t frame;
    ESP_RETURN_ON_ERROR(build_frame(dest, msg, body, body_len, &frame), TAG, "signing failed");
    return raw_send(dest, &frame);
}

esp_err_t db_proto_send(const uint8_t dest[DB_MAC_LEN], db_msg_t *msg)
{
    return db_proto_send_body(dest, msg, NULL, 0);
}

esp_err_t db_proto_send_hello(const uint8_t dest[DB_MAC_LEN], db_opcode_t opcode)
{
    ESP_RETURN_ON_FALSE(is_discovery(opcode), ESP_ERR_INVALID_ARG, TAG, "not a discovery opcode");
    const hello_frame_t hello = { .version = DB_PROTO_VERSION, .opcode = opcode };
    return espnow_send(ESPNOW_DATA_TYPE_DATA, dest, &hello, sizeof(hello), &s_hello_frame,
                       pdMS_TO_TICKS(SEND_TIMEOUT_MS));
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
    uint32_t counter = 0;  /* db_msg_t is packed: no pointers to its members */
    frame_t frame;
    esp_err_t err = next_counter(&counter);
    if (err == ESP_OK) {
        msg->version = DB_PROTO_VERSION;
        msg->counter = counter;
        err = build_frame(dest, msg, NULL, 0, &frame);
    }
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_pending = (pending_request_t) {
            .active = true, .ack_opcode = ack_opcode, .session = msg->session, .counter = counter,
        };
        memcpy(s_pending.dest, dest, DB_MAC_LEN);
        xSemaphoreGive(s_lock);
        xSemaphoreTake(s_ack, 0);  /* drop a stale signal */

        err = ESP_ERR_TIMEOUT;
        const int64_t start_us = esp_timer_get_time();
        int attempt = 0;
        for (; attempt <= retries && err != ESP_OK; attempt++) {
            db_wdt_feed();  /* the caller may be a supervised task; one attempt is short (checked above) */
            esp_err_t sent = raw_send(dest, &frame);  /* same counter and tag on every attempt */
            if (sent != ESP_OK) {
                ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(sent));
            }
            if (xSemaphoreTake(s_ack, pdMS_TO_TICKS(CONFIG_DB_ACK_TIMEOUT_MS)) == pdTRUE) {
                err = ESP_OK;
            }
        }
        /* ACK timeout measurement (SPEC 9). Retries carry the same counter, so a late ACK of the first attempt also
         * ends the wait: the time is counted from the first send. */
        const int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "opcode %u to " MACSTR " acknowledged after %d ms (attempt %d of %d)", msg->opcode,
                     MAC2STR(dest), elapsed_ms, attempt, retries + 1);
        } else {
            ESP_LOGW(TAG, "opcode %u to " MACSTR " not acknowledged after %d ms (%d attempts)", msg->opcode,
                     MAC2STR(dest), elapsed_ms, retries + 1);
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_pending.active = false;
        xSemaphoreGive(s_lock);
    }
    xSemaphoreGive(s_request_lock);
    return err;
}

void db_proto_get_stats(db_proto_stats_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    xSemaphoreGive(s_lock);
}

esp_err_t db_proto_forget_peer(const uint8_t mac[DB_MAC_LEN])
{
    char key[PEER_KEY_SIZE];
    peer_key(mac, key);
    ESP_RETURN_ON_ERROR(db_nvs_erase_key(NS_PEER_COUNTER, key), TAG, "erase peer counter failed");
    seen_clear(mac);
    return ESP_OK;
}

esp_err_t db_proto_erase_peer_counters(void)
{
    ESP_RETURN_ON_ERROR(db_nvs_erase_namespace(NS_PEER_COUNTER), TAG, "erase peer counters failed");
    seen_clear(NULL);
    return ESP_OK;
}

esp_err_t db_proto_erase_all_counters(void)
{
    ESP_RETURN_ON_ERROR(db_proto_erase_peer_counters(), TAG, "erase peer counters failed");
    ESP_RETURN_ON_ERROR(db_nvs_erase_namespace(NS_OWN_COUNTER), TAG, "erase own counter failed");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_counter = 0;
    s_reserved = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
