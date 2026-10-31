# 17. Request ids for retries

Date: 2026-11-01
Status: Accepted (amends ADR 0002 and ADR 0012)

## Context
Until now a retry reused the request's counter, and the receiver treated "same counter as the last accepted one"
as a retry: send the ACK again, do not act again.

Each device has one send counter, but two of its tasks send: the application task sends requests and the protocol
task sends ACKs. If an ACK goes out between two attempts of a request, the receiver accepts the ACK's higher counter
first, and the retry's lower counter is then dropped as old. Example: a module's OPEN_DOOR (counter 10) is lost; the
panel rings the module at that moment and its RING_ACK (counter 11) is accepted; the OPEN_DOOR retry (counter 10) is
dropped and the door does not open. The same happens in the other direction, and when the mesh delivers frames out of
order.

Options:
- **Anti-replay window** (IPsec, RFC 4303; DTLS, RFC 6347): accept a counter below the newest one if it was not seen
  within the last N. Rejected: a lost OPEN_DOOR captured by an attacker could then be replayed later, while it is
  still inside the window, and would open the door.
- **Fresh counter per frame, request id per transaction** (BLE Mesh: strictly increasing SEQ plus the TID of the
  models, which marks retransmitted messages; CoAP: the Message ID). Chosen.

## Decision
- Every frame, including each retry, carries a new counter, and the receiver accepts only a counter newer than any
  accepted from that peer. No frame is accepted twice, before or after a restart.
- A command (RING, OPEN_DOOR, PAIR_CONFIG) carries `request_id`: the counter of its first attempt, the same on every
  retry. An ACK carries the `request_id` it acknowledges. Reports carry 0. The field replaces `ack_counter`; the frame
  size is unchanged.
- The receiver stores, per peer, the newest command counter and the request id it executed last (NVS namespace
  `db_peer`). A command with that request id is a retry: the ACK is sent again, the action is not repeated.
- ACKs and reports are checked against the newest counter in RAM only: an old ACK names an old request id and is
  ignored, and a report changes no state. This saves an NVS write per ACK.
- A request waits for an ACK with its request id, so a late ACK of the first attempt also completes it.

## Consequences
- A retry is never lost because the same device sent something in between, or because frames arrived out of order:
  each retry is newer than everything before it.
- A command is still executed at most once per request id, also across a restart of the receiver.
- A captured frame is never accepted later, inside or outside any window.
- Each retry is signed again (one more HMAC per retry).
- Old firmware is not compatible: both roles must be flashed together, which every NVS layout change already
  requires.
