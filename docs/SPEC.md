# ESP32 Smart Apartment Doorbell and Access Control System — Specification

Version: 2026-10-29
Sources: original project specification + `docs/adr/0001`–`0017`

> Every decision in this document links to its ADR. Values not yet verified by measurement or test are marked **[TO VERIFY]** and listed in section 9.

---

## 1. Architecture

- Communication: ESP-NOW via Espressif's official `espressif/esp-now` component (v2.5.3, ESP-IDF ≥ 4.4). No custom flooding engine. ([ADR 0001](adr/0001-espressif-esp-now-component.md))
- Connectionless: there is no AP-STA connection between nodes.
- Multi-hop delivery uses the component's forwarding feature (only broadcast packets are forwarded).
- Target latency budget: 2 s to the farthest apartment. Actual latency **[TO VERIFY]**.

### 1.1. Topology
- One outdoor panel and 1–99 apartment modules per building.
- All nodes are on the same Wi-Fi channel ([ADR 0006](adr/0006-channel-selection.md)).
- The panel is the single central authority; if the panel is down, the system is down.
- Signal penetration between floors and path continuity when an intermediate node is lost **[TO VERIFY]**.

---

## 2. Hardware and GPIO Map

Both modules use a **NodeMCU-32S (ESP32-WROOM-32)**, chip revision **v3** (verified with `esptool.py chip_id`).

### 2.1. Outdoor Module (Building Entrance Panel)
Exactly one per building.

| Function | GPIO | Details |
| :--- | :---: | :--- |
| Keypad rows | 13, 14, 27, 26 | R0–R3 in order (3x4 matrix). Pin order 13-16. |
| Keypad columns | 25, 33, 32 | C0–C2 in order. |
| Door relay | 23 | Active-LOW, idle HIGH. 3.3 V optocoupled relay module. ([ADR 0005](adr/0005-door-relay-output.md)) |
| Buzzer | 4 | Passive buzzer, LEDC PWM. Sounds only while the door opens. |
| Status LED | 2 | On-board LED, brightness via LEDC. |
| Restart | EN | Hardware reset; optional hidden button between EN and GND. Erases nothing. ([ADR 0016](adr/0016-watchdog-supervision.md)) |
| Hidden code reset button | 19 | Internal pull-up, pulls to GND when pressed. Inside the enclosure. |
| Spare | 18, 21, 22 | Reserved for future use. |
| Power | — | 5 V / 1 A DC adapter. The relay coil draws ~120 mA from the 3.3 V rail. |

### 2.2. Indoor Module (Apartment Unit)
One per apartment; 1–99 apartments.

| Function | GPIO | Details |
| :--- | :---: | :--- |
| Bell relay | 4 | Active-HIGH. External pull-down recommended. |
| Door-open button | 23 | Internal pull-up, pulls to GND when pressed. Presses under 100 ms or over 30 s are ignored. Triggers on release. |
| Hidden pairing button | 18 | Internal pull-up. Hold 7–12 s and release to enter pairing mode. |
| Hidden reset button | 19 | Internal pull-up. Hold 7–12 s and release to erase the identity. |
| Status LED | 2 | On-board LED, brightness via LEDC. |
| Restart | EN | Hardware reset; optional hidden button between EN and GND. Erases nothing. ([ADR 0016](adr/0016-watchdog-supervision.md)) |
| Spare | 13, 14, 21, 22, 25, 26, 27, 32, 33 | |

On the NodeMCU-32S, GPIO 18, GPIO 19 and GND are adjacent; both hidden buttons share one GND line.

### 2.3. Door relay rules ([ADR 0005](adr/0005-door-relay-output.md))
- The system is only responsible for driving GPIO 23; the lock type is the installer's responsibility.
- Door open: GPIO 23 **LOW for 0.5 s**, then HIGH. Timed with an `esp_timer` one-shot timer.
- The buzzer starts together with the relay and stops after 0.5 s.
- A second door-open command during the pulse is ignored.
- At init the output level is set HIGH first, then the pin is switched to output mode.
- The door must stay locked on power loss. The relay must not engage on power-up, reset or flashing **[TO VERIFY]**.

---

## 3. Usage

### 3.1. Visitor rings
```text
<apartment_no> #        Example: 12 #
```
- Apartment numbers are 1–99.
- The apartment module pulses its bell relay for 0.5 s and sends an ACK to the panel.
- The panel LED stays on for 2 s.

### 3.2. Resident opens the door
- Presses and releases the door-open button on the apartment module.
- The panel drives the door relay and the buzzer for 0.5 s (section 2.3).

### 3.3. Opening the door with a code at the panel
```text
* <user_code> #
```
There is one user code for the whole building ([ADR 0008](adr/0008-single-building-user-code.md)).

### 3.4. Cancelling input
```text
* #
```
Input is also cleared automatically after 10 s of inactivity.

---

## 4. Administration and Codes

| Code | Length | Factory value | Permission |
| :--- | :---: | :--- | :--- |
| User code | 4 digits | Per device, on the label | Opens the door |
| Admin code | 6 digits | Per device, on the label | Administration and pairing |

- Factory codes are generated randomly in production, written with `mfg_gen.py` to a read-only factory NVS partition and printed on a label ([ADR 0003](adr/0003-per-device-passwords.md)).
- Change the user code: `* <admin_code> * 3 * <new:4> * <new:4> #`
- Change the admin code: `* <admin_code> * 4 * <new:6> * <new:6> #`

### 4.1. Wrong-code protection
- After 5 wrong codes in a row, input starting with `*` is locked for 60 s.
- If errors continue, the lockout doubles (120, 240 s, …) up to 15 min. After each lockout the failure count restarts;
  a correct code resets the lockout level (ADR 0012).
- Ringing is never blocked during a lockout.
- Malformed input with an empty field (two `*` in a row, or `*` right before `#`, for example `**#`) shows an error
  and is not counted as a wrong code.

---

## 5. Pairing ([ADR 0007](adr/0007-pairing-near-panel.md))

Pairing is done **next to the panel**. The espnow_sec key exchange only works within the panel's direct radio range.

### 5.1. Flow
1. **At the panel:** the admin enters:
   ```text
   * <admin_code> * 1 * <apartment_no> #
   ```
   The panel LED shows a repeating double flash; a 20 s pairing window opens. The panel stays on its own channel and waits for an unpaired module's HELLO during the window.
2. **At the module (next to the panel):** the installer holds the hidden pairing button (GPIO 18) for 7–12 s and releases it. The module starts as an espnow_sec responder and sends a HELLO on channels 1, 6 and 11 in turn until the panel answers.
3. **Panel scan result:**
   - Exactly 1 unpaired module found: the espnow_sec handshake runs; then the apartment number and channel are sent encrypted to the module, which stores them in NVS.
   - More than one unpaired module found: pairing is cancelled and the panel shows an error.
   - None found: an error is shown when the window ends.
4. **Confirmation:**
   - The panel LED stays on for 2 s; the record is written to NVS.
   - The module LED stays at full brightness for 2 s and the module pulses its bell relay for 0.5 s (audible if a bell is connected), then returns to normal state.
   - If the module is not paired within 20 s, it returns to the unpaired state.
5. The installer mounts the module in the apartment.

### 5.2. Rules
- No PoP is used (accepted risk, ADR 0007).
- An occupied apartment number is never overwritten; the request is rejected.
- A module is registered under one number only: pairing a reset module to another number replaces its old record.
- A paired module cannot be paired again without a reset (the responder ignores new handshakes once configured).
- Delete an apartment record: `* <admin_code> * 2 * <apartment_no> * <apartment_no> #`. Deleting also removes that apartment's replay counter record.

---

## 6. Reset

1. **Panel code reset:** hold the hidden button inside the panel enclosure (GPIO 19) for 10–60 s and release. Codes in normal NVS are erased; the panel falls back to the factory codes on the label. Apartment records are kept.
2. **Indoor module reset:** hold the hidden reset button (GPIO 19) for 7–12 s and release. Apartment identity, channel, key and counter are erased from NVS; the module returns to the unpaired state.
3. **Building reset:** `* <admin_code> * 6 * <admin_code> #` — erases all apartment records, counters, the health supervision state, the channel and the application key. The panel restarts and runs a channel scan (section 7.5).

---

## 7. Communication

### 7.1. Component configuration ([ADR 0001](adr/0001-espressif-esp-now-component.md))
- `frame_head.broadcast = true` (forwarding only works for broadcast packets)
- `frame_head.security = true`
- `frame_head.magic`: random (component default)
- `forward_ttl` ≤ 30 (31 = unlimited). Value **[TO VERIFY]**.
- `forward_rssi` / `filter_weak_signal`: value **[TO VERIFY]**.
- `esp_wifi_set_ps(WIFI_PS_NONE)`
- Receive handlers do not block; long work is queued and handled in a separate task (ESP-IDF ESP-NOW documentation).

### 7.2. Application message
Carried inside the encrypted payload:

```c
typedef enum {
    DB_OP_RING            = 1,  // Panel -> Apartment
    DB_OP_RING_ACK        = 2,  // Apartment -> Panel
    DB_OP_OPEN_DOOR       = 3,  // Apartment -> Panel
    DB_OP_OPEN_DOOR_ACK   = 4,  // Panel -> Apartment
    DB_OP_PAIR_CONFIG     = 5,  // Panel -> Apartment, after the espnow_sec handshake
    DB_OP_PAIR_CONFIG_ACK = 6,  // Apartment -> Panel
    DB_OP_PAIR_HELLO      = 7,  // Unpaired module broadcast; plaintext, single hop, 2 bytes
    DB_OP_PAIR_HELLO_ACK  = 8,  // Panel -> module; plaintext, single hop, 2 bytes
    DB_OP_HEARTBEAT       = 9,  // Apartment -> Panel, periodic health report (ADR 0015); no ACK
} db_opcode_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;      // Message format version (1)
    uint8_t  opcode;       // db_opcode_t
    uint8_t  apartment;    // Target or source apartment
    uint8_t  channel;      // PAIR_CONFIG: Wi-Fi channel; otherwise 0
    uint32_t session;      // Pairing session chosen by the panel; other sessions are dropped
    uint32_t counter;      // Sender's replay counter (ADR 0002)
    uint32_t request_id;   // Commands: counter of the first attempt, same on retries;
                           // ACKs: the request_id acknowledged; reports: 0 (ADR 0017)
} db_msg_t;
```

Multi-byte fields are little-endian; the packed structure is sent as is.

Every message is followed by an opcode-specific body of 0 to 32 bytes (only `HEARTBEAT` has one: `db_health_t`,
30 bytes) and a 16-byte HMAC-SHA256 tag over source MAC, destination MAC, message and body, with a key derived from
the application key (32 to 64-byte frame). esp-now also delivers unencrypted frames to the application, so frames
without a valid tag are dropped. HELLO frames carry no tag and are only used to find the channel.

The key exchange is the espnow_sec handshake; `PAIR_CONFIG` then carries the apartment number and channel
(`components/db_proto`, [ADR 0012](adr/0012-protocol-and-application-logic.md)).

### 7.3. Replay protection ([ADR 0002](adr/0002-replay-protection-counter.md))
- Every sender increments its counter by 1 for each frame, retries included.
- The receiver keeps the newest accepted counter per sender and rejects any counter lower than or equal to it: no
  frame is accepted twice.
- A command's retries keep its `request_id`; the receiver executes a request id once and only acknowledges its
  retries ([ADR 0017](adr/0017-request-ids-for-retries.md)).
- Only a registered peer in its current session can change this state (ADR 0012).
- Counters are stored in NVS with block reservation. Block size **[TO VERIFY]**.

### 7.3.1. Health reports ([ADR 0015](adr/0015-health-reporting.md))
- Every paired module reports uptime, last reset reason and fault counters to the panel every
  `DB_HEARTBEAT_INTERVAL_S` (default 3600 s, +/-10 % jitter); the panel logs each report.
- The panel logs a registered module that sends no report for 3 intervals as missing.
- Reports and ACKs are checked against the newest counter in RAM only; commands keep their state in NVS.

### 7.4. Application ACKs and retries
- The panel sends `RING` and waits for `RING_ACK`; if none arrives it sends 2 more times.
- The apartment module sends `OPEN_DOOR` and waits for `OPEN_DOOR_ACK`; if none arrives it sends 1 more time.
- Each retry is a new frame with a new counter and the same `request_id`; the receiver does not repeat the action,
  it only resends the ACK (ADR 0017).
- ACK timeout **[TO VERIFY]**. The 400 ms of the original specification contradicted its own one-way latency figure (250–300 ms).

### 7.5. Channel ([ADR 0006](adr/0006-channel-selection.md))
- Candidates: 1, 6, 11.
- If no channel is stored in NVS (first installation or after a building reset), the panel runs a site scan at boot, selects the channel with the fewest APs (on a tie, the lowest maximum RSSI) and stores it in NVS.
- The channel is sent to the module during pairing.
- The channel cannot be changed after installation; if it must change, all apartments are paired again.

### 7.6. RF power
- Lab: 10 dBm (`esp_wifi_set_max_tx_power(40)`); ESP-IDF maps 40 to 34, i.e. 8.5 dBm actual.
- Field: 20 dBm (`esp_wifi_set_max_tx_power(80)`).

---

## 8. Software and Deployment ([ADR 0004](adr/0004-secure-boot-flash-encryption.md))

| | Development (`sdkconfig.defaults`) | Production (`sdkconfig.defaults.prod`) |
|---|---|---|
| `CONFIG_ESP32_REV_MIN` | v3.0 | v3.0 |
| Secure Boot v2 | Off | On |
| Flash Encryption | Off | On, Release mode |
| NVS Encryption | Off | On (`nvs_keys`) |
| eFuse burning | None | On the production line |
| Flashing | Merged image at `0x0` (`builds/<role>_YYYY-MM-DD_HH-MM-SS.bin`) | To be defined |

- No eFuse is burned during development; every change is reversible.
- The merged image is produced by `tools/build.py`: for each role it runs `idf.py build` and `idf.py merge-bin` (raw format).
  In raw format esptool pads the gaps between files with `0xFF` (esptool 5.6.0 source code), so flashing at `0x0` also erases the NVS region.
- No OTA: the system is closed and never communicates outside its ESP-NOW network; firmware is changed only by
  serial flashing on site ([ADR 0013](adr/0013-closed-system-no-ota.md)).
- Partition table: ESP-IDF single-app layout plus the factory NVS partition (`mfg_gen.py`), `nvs_keys` in production;
  no OTA partitions. Current panel table: single-app layout plus `fctry` (0x110000, 0x6000, readonly).
- Accepted risks: no PoP ([ADR 0007](adr/0007-pairing-near-panel.md)); shared application key, lockout in RAM, panel
  busy while pairing, flasher code lengths ([ADR 0014](adr/0014-accepted-risks.md)).
- Self-recovery ([ADR 0016](adr/0016-watchdog-supervision.md)): the task watchdog (30 s) restarts the device and
  supervises the protocol and application tasks; interrupt watchdog, panic restart and brownout detector stay on.
  The panel restarts itself when no module has reported for 3 heartbeat intervals, with a doubling wait after each
  restart in a row (up to 48 intervals).
- Build and flashing GUI: `python -m tools.flasher` ([ADR 0010](adr/0010-flasher-gui.md)). Per-device factory NVS is generated at flash time; there is no per-device firmware build.

---

## 9. To Verify

| # | Item | Method |
|---|---|---|
| 1 | espnow_sec handshake is single-hop | Bench test |
| 2 | ~~Whether the espnow_sec PoP parameter accepts NULL~~ — No (`ESP_PARAM_CHECK`); fixed non-secret PoP used (ADR 0012) | Source code (done) |
| 3 | Dwell time per channel while the module waits for HELLO_ACK (proposed 500 ms) | Bench test: 10 pairings, panel on each candidate channel |
| 4 | Whether packets encrypted with a different key are forwarded — Yes per source code (forwarded before decryption) | Bench test |
| 5 | ~~How the espnow_sec application key is generated~~ — `esp_fill_random()` on the panel, as in the esp-now security example | Source code (done) |
| 6 | End-to-end latency and ACK timeout | Field measurement: `db_proto` logs each request's time to ACK, counted from the first send |
| 7 | `forward_ttl` and `forward_rssi` values | Field measurement: `db_proto` logs the RSSI of every authentic frame (last hop) |
| 8 | Signal penetration between floors, delivery when an intermediate node is lost | Field measurement |
| 9 | Door relay does not engage on power-up / reset / flashing | Measurement: oscilloscope on GPIO 23, single trigger on a falling edge |
| 10 | Voltage drop on the 3.3 V rail while the relay engages | Measurement: oscilloscope on 3V3; no brownout reset in the log |
| 11 | Replay counter block size | Usage data |
| 12 | A supervised task forced into an endless wait restarts the device; the panel logs reset reason "task watchdog" | Bench test |

## 10. To Define

- ~~Format of the post-pairing configuration message~~ — `DB_OP_PAIR_CONFIG` (section 7.2).
- Production flashing flow (production phase).

---

## 11. LED States

### 11.1. Outdoor panel
| Pattern | Meaning |
| :--- | :--- |
| Off | Idle, ready |
| Single short flash | Key pressed |
| Slow blink (1 Hz) | Command being entered |
| Solid 2 s | Success / bell rang / paired |
| Solid during relay pulse + buzzer (0.5 s) | Door opened |
| Fast blink for 1 s | Error / invalid code / rejected / apartment occupied / more than one module found |
| Medium blink for 3 s | Apartment registered but not responding |
| Repeating double flash | 20 s pairing window open |
| Sparse single flashes | Keypad locked after wrong codes |

### 11.2. Indoor module
| Pattern | Meaning |
| :--- | :--- |
| Repeating double flash | Unpaired |
| Dim, solid | Normal, paired |
| Fast blink | Bell ringing (visual bell) |
| Medium blink | Pairing button held (release within 7–12 s) |
| Slow blink (1 Hz) | Pairing mode: searching for the panel's channel |
| Full brightness 2 s | Pairing successful |
| Solid with occasional blink | Reset button held (release within 7–12 s) |
| Repeating triple flash | Button held longer than 15 s; cancelled |
