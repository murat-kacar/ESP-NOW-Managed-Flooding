# 12. Protocol and application logic

Date: 2026-10-30
Status: Accepted

## Context
Facts from the espressif/esp-now 2.5.3 sources (`managed_components/espressif__esp-now`):
- The receive handler gets the original sender's address (`espnow_data->src_addr`), not the forwarding node's.
- A broadcast frame whose `dest_addr` is a specific MAC is forwarded by other nodes and processed only by that device.
  Forwarding happens in the receive path before decryption, so frames encrypted with another key are still forwarded.
- Data handlers run in the component's own task, not in the Wi-Fi task.
- espnow_sec: `espnow_sec_initiator_scan()` sends up to 5 requests 500 ms apart; an unconfigured responder reports
  `ESPNOW_SEC_VER_NONE`. After a successful handshake the responder calls `espnow_set_key()` and `espnow_set_dec_key()`,
  stores the key, and posts `ESP_EVENT_ESPNOW_SEC_OK` with the initiator's MAC. `pop_data` must not be NULL.
- The security example creates the application key with `esp_fill_random()` on the initiator.
- `esp_wifi_set_max_tx_power()` must be called after `esp_wifi_start()`; 40 maps to 34 (8.5 dBm), 80 to 80 (20 dBm).
  The default country is "01" (world safe mode).

## Decision
### Components
- `db_net`: Wi-Fi/ESP-NOW init with `sec_enable`, TX power, optional country code (802.11d off), Long Range,
  channel set/get, site scan, application key (`db_net_ensure_key()` = random key on the panel), PoP `"doorbell"`
  (non-secret, ADR 0007).
- `db_proto`: message format, authentication tag, replay counters, ACK and retries. Frames: broadcast,
  `security = true`, `dest_addr` = peer MAC, TTL/RSSI/retransmit/weak-signal filter from Kconfig. The esp-now handler
  only copies the frame to a queue; a protocol task processes it (SPEC 7.1).

### Message authentication
esp-now 2.5.3 delivers a frame received without encryption to the data handler exactly like a decrypted one
(`espnow.c` receive path), so the handler cannot tell them apart and anyone could inject plaintext messages.
Every application message therefore carries a 16-byte tag:
- `K_auth = HKDF-Expand(application key, "doorbell message auth", 32)` (RFC 5869; the 32-byte random application key
  is the PRK, one block: `HMAC-SHA256(key, info || 0x01)`).
- `tag = HMAC-SHA256(K_auth, src MAC || dest MAC || message)` truncated to 16 bytes, verified with
  `psa_mac_verify()` (constant time). PSA Crypto API of ESP-IDF 6.1 (TF-PSA-Crypto).
- Frames of any other size are dropped. The source MAC is the original sender reported by esp-now.

### Pairing discovery
The only plaintext frames are `PAIR_HELLO` (7, module broadcast) and `PAIR_HELLO_ACK` (8, panel to the module's MAC):
2 bytes (`version, opcode`), `forward_ttl = 0` (single hop, ADR 0007), no content to trust beyond "a module is here".
Why: in the first bench test the module hopped to the next channel in the middle of the handshake (security log at
56.2-56.5 s, hop at 56.6 s), because the panel's scan and the module's hop were not synchronised. With HELLO the module
waits on a channel only until the panel answers, and the panel starts the handshake only after it has answered.

### Message
`version, opcode, apartment, channel, session, counter, ack_counter` (16 bytes) plus the 16-byte tag. Opcodes 1-4 as
in SPEC 7.2, plus `PAIR_CONFIG` (5, panel to module: apartment, channel and session) and `PAIR_CONFIG_ACK` (6).
An ACK carries its own counter, the counter of the acknowledged request and the request's session.

### Pairing session
The panel picks a random `session` (`esp_random()`) at every pairing, stores it with the module's MAC in the registry
and sends it in `PAIR_CONFIG`; the module stores it in its identity. Every message carries it, and a message with a
different session is dropped (the same idea as the BLE Mesh IV index).
Why: a module reset, the flasher's Factory reset, and flashing at 0x0 all erase the module's NVS, including its own
send counter, so after re-pairing it counts from 1 again. With a new session, every message captured in the old
session is invalid, so the panel can forget that MAC's counter at pairing (`db_proto_forget_peer()`).

### Replay counters (ADR 0002)
- Own send counter: NVS namespace `db_ctr_own`, reserved in blocks of `DB_REPLAY_COUNTER_BLOCK`; after a reboot
  counting resumes above the reserved block. Erased by a building reset (which also replaces the key) or an NVS erase.
- Peer counters: NVS namespace `db_ctr_peer`, one entry per MAC, written on every accepted message (messages are rare).
  The panel forgets a MAC's counter when it pairs that MAC with a new session. A module reset erases the peer counters
  it holds (the panel's).
- Same counter as the last accepted one = retry: the ACK is sent again, the action is not repeated.
- A message whose counter cannot be stored is not accepted.

### Panel
- Keypad: espressif/button matrix (row-major, key map from Kconfig); keys are processed in one application task.
- Commands (SPEC 3, 4, 5, 6) with the wrong-code lockout. Interpretation where SPEC is silent: after each lockout
  the failure count restarts, the next lockout is multiplied by `DB_LOCKOUT_MULTIPLIER` up to the maximum, and a correct
  code resets the level.
- Codes: changed codes in NVS `db_codes`, else the factory partition `fctry` (namespace `factory`); constant-time compare.
- Registry: NVS `db_registry`, one MAC per apartment, cached in RAM.
- Pairing: wait for the first `PAIR_HELLO` in the window, then listen for one module cycle
  (`3 x DB_CHANNEL_DWELL_MS`); more than one module MAC: cancelled. Then `PAIR_HELLO_ACK` and the espnow_sec handshake
  (`espnow_sec_initiator_start` with that MAC), repeated until success or the end of the window; then `PAIR_CONFIG`
  with up to 2 retries, then register. `DB_CHANNEL_DWELL_MS` is a shared option in `components/db_net/Kconfig`.
- Channel: NVS `db_panel`; site scan at boot when missing. If the scan fails, the first candidate is used and nothing
  is stored, so the next boot scans again.
- OPEN_DOOR is accepted only from the MAC registered for the message's apartment number, in its current session.

### Apartment module
- Identity (apartment, channel, panel MAC) in NVS `db_identity`; the key is stored by esp-now.
- Pairing button (only when unpaired): responder on; on each candidate channel it broadcasts `PAIR_HELLO` and waits
  `DB_CHANNEL_DWELL_MS` for `PAIR_HELLO_ACK`. The first answer locks the channel and the panel MAC; it then accepts
  `ESP_EVENT_ESPNOW_SEC_OK` and `PAIR_CONFIG` only from that panel.
  An unpaired module holds no key: a window that ends without configuration erases any stored key (the responder
  stores the key of whichever initiator completes first), and a key found at boot without an identity is erased.
- RING is accepted only from the stored panel MAC for the stored apartment number and session.
- Reset button: erases identity, key and peer counters, then restarts.

## Consequences
- Every feature uses its Kconfig options, so GUI values reach the firmware.
- Not verified on hardware: timings, ranges, pairing, multi-hop delivery.
