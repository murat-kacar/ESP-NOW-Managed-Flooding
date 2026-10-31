# 2. Replay protection: monotonic counter

Date: 2026-10-29
Status: Accepted; retry handling amended by ADR 0017

## Context
espressif/esp-now only filters the last 32 packets. Older encrypted packets can be captured and replayed (e.g. `OPEN_DOOR`).

## Decision
Apply the BLE Mesh SEQ / Zigbee / IEEE 802.15.4 frame counter pattern:
- Every sender increments its counter by 1 for each message; the counter is carried inside the encrypted payload.
- The receiver keeps the last accepted counter per sender and rejects any counter lower than or equal to it.
  Retries are recognised by a request id, not by a repeated counter (ADR 0017).
- Counters are stored in NVS. Block reservation limits flash wear (block size to be set from usage data).

## Alternative considered
Challenge-response: needs no persistent state, but takes 3 messages and roughly 3x the latency.

## Consequences
- In NVS, the panel stores the last counter of each apartment module, and each apartment module stores the last counter of the panel.
- Deleting a pairing also deletes its counter record.
