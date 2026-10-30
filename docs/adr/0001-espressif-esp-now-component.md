# 1. Communication core: espressif/esp-now component

Date: 2026-10-29
Status: Accepted

## Context
The original specification planned a custom flooding engine (`db_mesh_pkt_t`, deduplication ring buffer, jitter, TTL, ACK).
Espressif's official `espressif/esp-now` component (v2.5.3, ESP-IDF >= 4.4, Apache-2.0) already provides these.

## Decision
Communication is built on `espressif/esp-now`. Features used:
- Forwarding: `forward_ttl`, `forward_rssi`, `filter_weak_signal`
- Duplicate filter: `magic` (random, 32-entry cache)
- Reliability: `ack`, `retransmit_count`
- Security: espnow_sec (ECDH + PoP key exchange, AES128-CCM, random IV per packet)
- OTA: the component's OTA module

## Consequences
- The custom flooding code, CRC-16, jitter and the `seq` field are removed.
- The component has no replay protection; it is added at the application layer (see ADR 0002).
- PoP handling: see ADR 0007.
- `forward_ttl` must be at most 30: at `ESPNOW_FORWARD_MAX_COUNT` (31) the TTL is not decremented, which means unlimited forwarding.
- Open: whether packets encrypted with a different key are still forwarded must be verified by a bench test.
