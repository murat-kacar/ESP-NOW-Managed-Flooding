# 13. Closed system without OTA

Date: 2026-10-31
Status: Accepted

## Context
Product decision: the devices communicate only with each other over ESP-NOW. They never connect to an access point,
the internet or a cloud service, and they have no remote update.

ADR 0001 listed the `espressif/esp-now` OTA module as a feature to use, and SPEC 8 planned OTA partitions.

ETSI EN 303 645 (consumer IoT baseline) expects software to be updatable. For a device whose software is not updated,
it asks the manufacturer to publish the reason and how hardware replacement is supported.

## Decision
- No OTA. The esp-now OTA module is not used, and the partition table has no OTA partitions: it is the ESP-IDF
  single-app layout plus the factory NVS partition `fctry`, and `nvs_keys` in production (ADR 0003, ADR 0004).
- The Wi-Fi station runs only as the ESP-NOW radio. It never connects to an access point. The panel's site scan
  (ADR 0006) only listens for beacons.
- Firmware is changed only by serial flashing on site. Whether a deployed production device can still be flashed
  over serial depends on how the production flash-encryption key is handled. This is decided together with the
  production flashing flow (ADR 0004, SPEC 10).

## Consequences
- No remote attack surface: no connection beyond the ESP-NOW network and no remote update channel.
- A bug or vulnerability found after installation needs on-site work: serial flashing, or replacing the device if
  serial flashing is locked in production. The published product information must state this (EN 303 645).
- No firmware rollback is needed. Stored-data migration between firmware versions is not needed while every flashing
  erases NVS (merged image at 0x0, SPEC 8); the production flashing flow must keep or revisit this.
