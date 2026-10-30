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
- `db_proto`: message format, replay counters, ACK and retries. Frames: broadcast, `security = true`,
  `dest_addr` = peer MAC, TTL/RSSI/retransmit/weak-signal filter from Kconfig. The esp-now handler only copies the
  message to a queue; a protocol task processes it (SPEC 7.1).

### Message
`version, opcode, apartment, channel, counter, ack_counter` (12 bytes). Opcodes 1-4 as in SPEC 7.2, plus
`PAIR_CONFIG` (5, panel to module: apartment and channel) and `PAIR_CONFIG_ACK` (6).
An ACK carries its own counter and the counter of the acknowledged request.

### Replay counters (ADR 0002)
- Own send counter: NVS namespace `db_ctr_own`, reserved in blocks of `DB_REPLAY_COUNTER_BLOCK`; after a reboot
  counting resumes above the reserved block. Erased only by a building reset (which also replaces the key).
- Peer counters: NVS namespace `db_ctr_peer`, one entry per MAC, written on every accepted message (messages are rare).
  Not erased when a pairing is deleted, because the key stays the same and an old captured message would otherwise be
  accepted again. A module reset erases the peer counters it holds (the panel's), not its own send counter.
- Same counter as the last accepted one = retry: the ACK is sent again, the action is not repeated.
- A message whose counter cannot be stored is not accepted.

### Panel
- Keypad: espressif/button matrix (row-major, key map from Kconfig); keys are processed in one application task.
- Commands (SPEC 3, 4, 5, 6) with the wrong-code lockout. Interpretation where SPEC is silent: after each lockout
  the failure count restarts, the next lockout is multiplied by `DB_LOCKOUT_MULTIPLIER` up to the maximum, and a correct
  code resets the level.
- Codes: changed codes in NVS `db_codes`, else the factory partition `fctry` (namespace `factory`); constant-time compare.
- Registry: NVS `db_registry`, one MAC per apartment, cached in RAM.
- Pairing: scan in 1 s steps until exactly one unconfigured responder answers within the window (more than one:
  cancelled), handshake, then `PAIR_CONFIG` with up to 2 retries, then register. The short scan leaves at least
  `DB_CHANNEL_DWELL_MS - 2 s` of the module's channel dwell for the handshake (checked with `_Static_assert`);
  `DB_CHANNEL_DWELL_MS` is therefore a shared option in `components/db_net/Kconfig`.
- Channel: NVS `db_panel`; site scan at boot when missing. If the scan fails, the first candidate is used and nothing
  is stored, so the next boot scans again.
- OPEN_DOOR is accepted only from the MAC registered for the message's apartment number.

### Apartment module
- Identity (apartment, channel, panel MAC) in NVS `db_identity`; the key is stored by esp-now.
- Pairing button (only when unpaired): responder on, channel hopping every `DB_CHANNEL_DWELL_MS`; after
  `ESP_EVENT_ESPNOW_SEC_OK` it stays on that channel and accepts `PAIR_CONFIG` only from that panel.
  A window that ends after the handshake but without configuration erases the key.
- RING is accepted only from the stored panel MAC for the stored apartment number.
- Reset button: erases identity, key and peer counters, then restarts.

## Consequences
- Every feature uses its Kconfig options, so GUI values reach the firmware.
- Not verified on hardware: timings, ranges, pairing, multi-hop delivery.
