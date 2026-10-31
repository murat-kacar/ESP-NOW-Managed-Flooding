# 16. Watchdog supervision and restart paths

Date: 2026-10-31
Status: Accepted

## Context
An unattended device must recover by itself from a fault that stops it from working. In embedded practice this is
done in layers: a watchdog that supervises program flow, the panic handler, the brownout detector, and a hardware
reset (IEC 61508 lists watchdogs and program-flow monitoring among its diagnostic measures).

The ESP-IDF 6.1 defaults of both roles (from the generated `sdkconfig`):
- Interrupt watchdog on (300 ms), bootloader watchdog on, panic handler prints and restarts, brownout detector on.
- Task watchdog on with a 5 s timeout, but `ESP_TASK_WDT_PANIC` off: a triggered task watchdog only prints a
  warning and does not restart. ADR 0015 assumed a restart; that was wrong and is corrected here.
- The task watchdog supervises only the idle tasks. It notices a task that keeps the CPU busy, but not a task that
  is blocked for ever (a deadlock): the idle task still runs.

## Decision
1. **Task watchdog restarts the device:** `ESP_TASK_WDT_PANIC=y` in both `sdkconfig.defaults`. The restart is
   reported by the next heartbeat with the reset reason "task watchdog" (ADR 0015).
2. **Own tasks are supervised:** the protocol task (`db_proto`) and the application task of each role subscribe to
   the task watchdog (`components/db_wdt`). A supervised task waits for work with a timeout of a third of the
   watchdog timeout (`DB_WDT_FEED_TICKS`) and feeds the watchdog on every loop pass. Long operations feed at their
   steps: each `db_proto_request()` attempt, each pairing handshake pass, and pairing waits cut into feed periods. A
   build-time check keeps one request attempt shorter than the watchdog timeout.
3. **Timeout 30 s:** the longest operation that cannot feed in between is one espnow_sec handshake, about 3 s for
   one module (2 rounds of 1.5 s in esp-now 2.5.3 `espnow_sec_initiator_start()`). The ESP-IDF default of 5 s would
   leave too little margin. For a doorbell, a restart within 30 s of a fault is acceptable.
4. **Heap figures in the heartbeat:** free heap and the lowest free heap since the restart, so a slow degradation
   (for example a memory leak) is visible at the panel before it causes a failure.
5. **Hardware restart:** the board's EN pin resets the chip. A hidden button between EN and GND inside the
   enclosure gives a restart without erasing anything. It needs no firmware.

## Alternatives considered
- **Scheduled restart** (for example every night, "software rejuvenation"): treats the symptom and hides the cause.
  The heap figures show whether it is ever needed.
- **Restart the panel when no module reports:** a power cut in the building would make the panel restart over and
  over. Not added without field data.
- **External watchdog IC:** independent of the chip and the most robust option, but more than this product needs.
- **Network-wide restart command:** rejected in ADR 0015.

## Consequences
- A blocked or busy protocol task or application task restarts the device within 30 s.
- Short-lived tasks (the module's pairing discovery task) and the tasks of ESP-IDF and the managed components are not
  supervised by this project; the idle-task check still covers CPU starvation by any task.
- Bench test: a supervised task forced into an endless wait must restart the device, and the panel must log the
  module's reset reason as "task watchdog" (SPEC 9).
