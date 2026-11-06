# 19. Forwarding without an RSSI filter; explicit Long Range rate

Date: 2026-11-06
Status: Accepted (amends ADR 0001 and ADR 0010)

## Context
The range in the building test was too low (2026-11-06). The transmit power was already at its maximum (20 dBm),
and ESP-NOW already uses its most robust standard rate (1 Mbit/s, 802.11b; ESP-NOW guide of ESP-IDF 6.1).

Two settings limit the reach of the network:
- **Forward RSSI threshold** (esp-now default -55 dBm): a module forwards only frames it received at or above the
  threshold. The modules that receive strongly are the ones close to the sender, whose rebroadcast covers little new
  ground; the modules that receive weakly, further away, are the ones that would extend the reach, and they do not
  forward. BLE Mesh managed flooding has no such filter: every relay forwards each new message once, and the TTL
  and the message cache bound the traffic. esp-now has both of those as well (`forward_ttl`, the magic cache).
- **Long Range (LR)**: about 4 dB better reception sensitivity than 802.11b, in theory 2 to 2.5 times the distance
  (ESP-IDF Wi-Fi driver guide, "LR Transmission Distance"). The ESP-NOW guide gives 1 Mbit/s as the default rate
  and `esp_now_set_peer_rate_config()` as the way to change it, while the ESP-IDF espnow example only adds LR to the
  protocols and states that it then sends at the LR rates. Which of the two holds was not verified.

## Decision
- **No RSSI filter by default** (`DB_FORWARD_RSSI_FILTER` off): `forward_rssi` is set to `INT8_MIN`. esp-now
  forwards when `forward_rssi <= rssi` and the RSSI is an 8-bit signed value, so every received frame qualifies.
  Each module still forwards each message once (magic cache) and only while the TTL lasts. With the filter on, the
  threshold is `DB_FORWARD_RSSI` (default -70 dBm), and `DB_FILTER_WEAK_SIGNAL` is available.
- **Long Range made explicit** (`DB_WIFI_LONG_RANGE`, still off by default): LR is added to the protocols as in the
  ESP-IDF example, and the broadcast peer, which carries every message (esp-now adds it in `espnow_init()`), gets the
  LR rate with `esp_now_set_peer_rate_config()`: 250 kbit/s (default of `DB_WIFI_LR_RATE`, best sensitivity) or
  500 kbit/s. 802.11b/g/n stay enabled, so the panel's site scan still sees access points. The espnow_sec handshake
  uses its own peers and keeps the default rate; pairing is done next to the panel anyway (ADR 0007).

## Consequences
- More modules forward: the traffic of one message is at most (modules x `DB_RETRANSMIT_COUNT`) frames, the bound
  ADR 0015 already assumed for the heartbeat.
- An LR frame stays 2 to 4 times longer on air than at 1 Mbit/s. With LR on, the retransmit count (10, the esp-now
  default) and the heartbeat interval should be revisited with measurements.
- Every device of a building must use the same LR setting.
- To verify in the building: reach with and without LR, and the RSSI of each link (SPEC 9).
