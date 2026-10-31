# 15. Health reporting to the panel

Date: 2026-10-31
Status: Accepted

## Context
The installer must be able to follow every failure from the panel's serial monitor, without a monitor on every floor.
Each kind of failure is seen by a different device:

| Failure | Seen by |
| :--- | :--- |
| A RING does not reach the module | The panel only (no ACK). The module never learns of it. |
| An OPEN_DOOR does not reach the panel | The module only (no ACK). |
| A frame arrives but is rejected (tag, version, counter) | The receiver only. |

Standard mesh solutions for this are the BLE Mesh Heartbeat and Health models and the Zigbee Diagnostics cluster:
nodes keep fault counters and report them to a collector.

Flooding cost: every frame is retransmitted `DB_RETRANSMIT_COUNT` times by its sender and by every node that forwards
it. For N modules that all forward, one report round costs about N x N x `DB_RETRANSMIT_COUNT` frames. With 99
modules and 10 retransmissions that is about 98 000 frames per round. Spread over 3600 s it is about 27 frames per
second; over 30 s it is about 3 300 frames per second, which no 1 Mbit/s channel can carry. These are estimates from
the configuration, not measurements.

Replay counters (ADR 0002) are stored in NVS on every accepted message, because messages were rare. A report every
few minutes from every module would make the panel write its NVS continuously and wear the flash.

## Decision
### Module heartbeat
- A paired module sends `DB_OP_HEARTBEAT` (9) to its panel every `DB_HEARTBEAT_INTERVAL_S`, with a random delay
  before the first report and +/-10 % random jitter on each interval. This keeps modules that start together (power
  restored to the building) from reporting at the same moment. It is not acknowledged: the next report replaces a
  lost one.
- The report body (`db_health_t`, counters since the module's last restart): uptime, reset reason
  (`esp_reset_reason()`), RINGs received, OPEN_DOOR requests sent and how many got no ACK, frames rejected by
  `db_proto`, the RSSI of the last authentic frame received (last hop), and free and lowest free heap (ADR 0016).
- `DB_HEARTBEAT` turns the feature off on both roles. Default interval 3600 s for the field; short intervals (for
  example 30 s) only for bench tests with a few modules (see the cost above).

### Panel
- Every report from a registered module (matching apartment number and session) is logged as one line.
- Supervision: a registered module whose last report is older than 3 intervals is logged as missing, once per
  interval, and logged again when its reports resume. Three intervals mean one or two lost reports raise no alarm.
- RING failures are already logged by `db_proto` (`not acknowledged after ... ms`).

### Protocol changes
- An authenticated frame is now `db_msg_t`, an opcode-specific body of 0 to `DB_BODY_MAX` (32) bytes, and the tag.
  The tag covers the body. The frame size gives the body length; frames of other sizes are dropped.
- Replay check by message class (amends ADR 0002):
  - Commands and their ACKs (RING, OPEN_DOOR, PAIR_CONFIG): checked against the counter stored in NVS, which is then
    updated. This is unchanged.
  - Reports (HEARTBEAT): checked against the newest counter seen from that peer, kept in RAM only. A report changes
    no state, so replaying one after a panel restart only shows stale numbers, and no flash write is needed.
  - A command is also rejected if its counter is not newer than the newest one seen in RAM.

### Remote restart: not implemented
A network-wide restart command exists in other standards (for example the LwM2M Device object's Reboot resource).
It is not added:
- An unstable device restarts itself: the interrupt watchdog, the panic handler and the brownout detector are on
  by default, and ADR 0016 makes the task watchdog restart the device and supervise the project's own tasks.
  (This ADR first said the default task watchdog restarts; it only warns. Corrected in ADR 0016.)
- The heartbeat reports the reset reason and uptime, so instability becomes visible at the panel and can be fixed
  at its cause. A remote restart would hide the cause.
- An authenticated command that restarts every device is also a way to take the whole building offline.

Reopen if field data show a failure that the device cannot detect itself. In that case the command would be
addressed to one device, not broadcast.

## Consequences
- The panel's monitor shows every failure kind in the table above: its own failed RINGs directly, and the modules'
  failed OPEN_DOOR requests and rejected frames through their next report.
- Not visible: through which relay a frame was lost. esp-now does not pass the remaining TTL to the application.
- Report traffic grows with the square of the module count; the interval must be chosen for the building size.
