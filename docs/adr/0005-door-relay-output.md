# 5. Door relay output (outdoor panel, GPIO 23)

Date: 2026-10-29
Status: Accepted

## Context
The door lock type depends on the installation site and is out of scope.
The system's only responsibility is to drive a 3.3 V, active-LOW, optocoupled relay module.
The door must stay locked on power loss.

## Decision
- GPIO 23, active-LOW. Idle level HIGH.
- On a door-open command, GPIO 23 is held LOW for 0.5 s, then driven HIGH.
- The pulse is timed with an `esp_timer` one-shot timer; no task is blocked.
- At init the output level is set HIGH first, then the pin is switched to output mode, so no LOW glitch occurs at boot.
- On a crash or watchdog reset the pin returns to its reset state and the relay is released.
- The buzzer (GPIO 4) starts together with the relay and stops after 0.5 s on the same timer.
- A second door-open command received while the pulse is active is ignored; the pulse is not extended.

## To verify (by measurement)
- The relay must not engage during power-up, reset or flashing. If it does, an external pull-up is added on GPIO 23.
- Voltage drop on the 3.3 V rail while the relay engages (brownout risk).
