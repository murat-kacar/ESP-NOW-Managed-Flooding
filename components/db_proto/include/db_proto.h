#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DB_PROTO_VERSION 1
#define DB_MAC_LEN       6

/** Application message opcodes (SPEC 7.2). */
typedef enum {
    DB_OP_RING = 1,             /**< Panel -> apartment */
    DB_OP_RING_ACK = 2,         /**< Apartment -> panel */
    DB_OP_OPEN_DOOR = 3,        /**< Apartment -> panel */
    DB_OP_OPEN_DOOR_ACK = 4,    /**< Panel -> apartment */
    DB_OP_PAIR_CONFIG = 5,      /**< Panel -> apartment after the espnow_sec handshake: apartment number and channel */
    DB_OP_PAIR_CONFIG_ACK = 6,  /**< Apartment -> panel */
} db_opcode_t;

/** Application message, carried inside the encrypted esp-now payload. */
typedef struct __attribute__((packed)) {
    uint8_t version;       /**< DB_PROTO_VERSION */
    uint8_t opcode;        /**< db_opcode_t */
    uint8_t apartment;     /**< Target or source apartment */
    uint8_t channel;       /**< DB_OP_PAIR_CONFIG: Wi-Fi channel; otherwise 0 */
    uint32_t session;      /**< Pairing session, chosen by the panel at pairing; messages of other sessions are dropped */
    uint32_t counter;      /**< Sender's replay counter (ADR 0002) */
    uint32_t ack_counter;  /**< ACK opcodes: counter of the acknowledged request; otherwise 0 */
} db_msg_t;

/** Result of a replay check (ADR 0002). */
typedef enum {
    DB_REPLAY_NEW,    /**< Counter is higher than the last accepted one; it is now stored */
    DB_REPLAY_RETRY,  /**< Counter equals the last accepted one: a retry; reply again, do not repeat the action */
    DB_REPLAY_OLD,    /**< Counter is lower: drop */
} db_replay_t;

/**
 * @brief Called in the protocol task for every valid non-ACK message.
 *
 * The handler checks that the sender is known before calling db_proto_check_counter().
 */
typedef void (*db_proto_handler_t)(const uint8_t src[DB_MAC_LEN], const db_msg_t *msg);

/** @brief Register the esp-now data handler and start the protocol task. db_net_init() must run first. */
esp_err_t db_proto_init(db_proto_handler_t handler);

/**
 * @brief Send a message to one device (broadcast frame addressed to dest, forwarded by other nodes).
 *
 * Fills version and a new counter.
 */
esp_err_t db_proto_send(const uint8_t dest[DB_MAC_LEN], db_msg_t *msg);

/** @brief Send an ACK opcode for a received request (same apartment and session). */
esp_err_t db_proto_ack(const uint8_t dest[DB_MAC_LEN], db_opcode_t ack_opcode, const db_msg_t *request);

/**
 * @brief Send a request and wait for its ACK (same session); retries reuse the same counter (SPEC 7.4).
 *
 * @return ESP_OK when the ACK arrived, ESP_ERR_TIMEOUT when all attempts went unanswered.
 *         Call from a task that may block; one request at a time per device.
 */
esp_err_t db_proto_request(const uint8_t dest[DB_MAC_LEN], db_msg_t *msg, db_opcode_t ack_opcode, int retries);

/** @brief Compare a sender's counter with the last accepted one and store it when new. */
db_replay_t db_proto_check_counter(const uint8_t src[DB_MAC_LEN], uint32_t counter);

/**
 * @brief Forget one peer's counter. Only safe together with a new session: messages captured in the old
 * session carry the old session value and are dropped, so the peer may start counting again (pairing).
 */
esp_err_t db_proto_forget_peer(const uint8_t mac[DB_MAC_LEN]);

/** @brief Erase the stored counters of all peers; this device's own send counter is kept (module reset). */
esp_err_t db_proto_erase_peer_counters(void);

/** @brief Erase all counters, including this device's own (building reset, which also replaces the key). */
esp_err_t db_proto_erase_all_counters(void);

#ifdef __cplusplus
}
#endif
