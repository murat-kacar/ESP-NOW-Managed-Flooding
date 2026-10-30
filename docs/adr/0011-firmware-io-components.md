# 11. Firmware I/O on official Espressif components

Date: 2026-10-30
Status: Accepted

## Context
The apartment module needs buttons with press-duration rules and status LED patterns (SPEC 2.2, 11);
the panel needs a 3x4 keypad, a status LED and a buzzer. Espressif publishes components for these:
- `espressif/button` 4.2.3 (ESP-IDF >= 4.0): GPIO and matrix buttons, debounce, press/release and multiple
  long-press thresholds (`BUTTON_LONG_PRESS_START` with `long_press.press_time`, inserted in sorted order),
  `iot_button_get_pressed_time()`. Thresholds are `uint16_t` milliseconds and must exceed the short-press time.
  Pull resistor direction follows `active_level`; `disable_pull` turns it off.
- `espressif/led_indicator` 2.1.2 (ESP-IDF >= 5.0, depends on `espressif/led_strip` 3.*): LEDC brightness,
  prioritized blink lists (lower index wins), `LED_BLINK_STOP` ends a one-shot list and the next active list resumes.
  With no active list the LED keeps its last level. It configures the LEDC timer itself from its Kconfig
  (`LEDC_TIMER_FREQ_HZ`, `LEDC_TIMER_BIT_NUM`).
Sources: the downloaded component sources in `managed_components/`.

## Decision
- Buttons use `espressif/button`; the panel keypad will use its matrix driver.
- `components/db_status_led` implements the SPEC 11 patterns as `led_indicator` blink lists on LEDC timer 0, channel 0.
  `DB_LED_OFF` is a permanent lowest-priority list so the LED is off when nothing else is active.
  Timed patterns (error 1 s, no response 3 s) are stopped by an `esp_timer`.
- Options duplicated by these components are removed from the project Kconfig:
  LED PWM frequency (LED Indicator menu) and keypad debounce/scan period (IoT Button menu).
- Timing combinations that cannot work are rejected at build time with `_Static_assert`
  (pattern pulses longer than their cycle, hold windows out of order, thresholds above 16 bits).
- The service button cancel time range is limited to 65 s because thresholds are 16-bit milliseconds.

## Apartment module behaviour implemented
- Bell relay (`main/bell.c`): output level set before the pin becomes an output; pulse with `esp_timer`;
  a ring request during a pulse is ignored; visual bell (fast blink) for the pulse duration when enabled.
- Door button: action on release after `DB_DOOR_BUTTON_MIN_MS` to `DB_DOOR_BUTTON_MAX_S`.
- Pairing and reset buttons: release window LED from `DB_SERVICE_HOLD_MIN_S` to `DB_SERVICE_HOLD_MAX_S`,
  cancel pattern from `DB_SERVICE_HOLD_CANCEL_S`; only a release inside the window triggers the action.
  SPEC does not define the indication between the window end and the cancel time; the LED shows the base state there.
- The network actions (OPEN_DOOR, RING, pairing, identity storage) are described in ADR 0012.
