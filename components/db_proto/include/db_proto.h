#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DB_PROTO_VERSION 1
#define DB_MAC_LEN       6   /**< equals ESPNOW_ADDR_LEN (checked in db_proto.c) */
#define DB_TAG_LEN       16  /**< truncated HMAC-SHA256 authentication tag */

/** Application message opcodes (SPEC 7.2). */
typedef enum {
    DB_OP_RING = 1,             /**< Panel -> apartment */
    DB_OP_RING_ACK = 2,         /**< Apartment -> panel */
    DB_OP_OPEN_DOOR = 3,        /**< Apartment -> panel */
    DB_OP_OPEN_DOOR_ACK = 4,    /**< Panel -> apartment */
    DB_OP_PAIR_CONFIG = 5,      /**< Panel -> apartment after the espnow_sec handshake: apartment number and channel */
    DB_OP_PAIR_CONFIG_ACK = 6,  /**< Apartment -> panel */
    DB_OP_PAIR_HELLO = 7,       /**< Unauthenticated discovery, unpaired module -> broadcast, single hop */
    DB_OP_PAIR_HELLO_ACK = 8,   /**< Unauthenticated discovery, panel in its pairing window -> module, single hop */
} db_opcode_t;

/**
 * Application message, sent as is: multi-byte fields are little-endian (both roles run on ESP32; checked at
 * build time in db_proto.c). On air it is followed by a DB_TAG_LEN tag:
 * HMAC-SHA256(K_auth, src MAC || dest MAC || message), truncated, where K_auth = HKDF-Expand(app key,
 * "doorbell message auth", 32) (RFC 5869; Extract is skipped because the app key is uniformly random).
 * esp-now hands plaintext and decrypted frames to the application in the same way, so authenticity is checked
 * here and does not depend on the transport encryption.
 */
typedef struct __attribute__((packed)) {
    uint8_t version;       /**< DB_PROTO_VERSION */
    uint8_t opcode;        /**< db_opcode_t */
    uint8_t apartment;     /**< Target or source apartment */
    uint8_t channel;       /**< DB_OP_PAIR_CONFIG: Wi-Fi channel; otherwise 0 */
    uint32_t session;      /**< Pairing session, chosen by the panel at pairing; messages of other sessions are dropped */
    uint32_t counter;      /**< Sender's replay counter (ADR 0002) */
    uint32_t ack_counter;  /**< ACK opcodes: counter of the acknowledged request; otherwise 0 */
} db_msg_t;

/**
 * @brief Called in the protocol task for every authenticated non-ACK message that passed the replay check
 * (ADR 0002): its counter is higher than, or equal to, the last one accepted from that sender.
 *
 * @param retry  true when the counter equals the last accepted one: a retry of a message already delivered.
 *               Reply again, do not repeat the action (SPEC 7.4).
 */
typedef void (*db_proto_handler_t)(const uint8_t src[DB_MAC_LEN], const db_msg_t *msg, bool retry);

/**
 * @brief Called in the protocol task for an unauthenticated pairing discovery frame (DB_OP_PAIR_HELLO or
 * DB_OP_PAIR_HELLO_ACK). It grants nothing: the handshake and the authenticated PAIR_CONFIG follow.
 */
typedef void (*db_proto_hello_handler_t)(const uint8_t src[DB_MAC_LEN], db_opcode_t opcode);

/**
 * @brief Register the esp-now data handler and start the protocol task. db_net_init() must run first.
 *
 * @return ESP_FAIL or an NVS error if the stored send counter cannot be read: sending with an unknown counter could
 *         reuse values.
 */
esp_err_t db_proto_init(db_proto_handler_t handler, db_proto_hello_handler_t hello_handler);

/** @brief Send a pairing discovery frame (plaintext, not forwarded). dest may be the broadcast address. */
esp_err_t db_proto_send_hello(const uint8_t dest[DB_MAC_LEN], db_opcode_t opcode);

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
