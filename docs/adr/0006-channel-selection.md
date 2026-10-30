# 6. Wi-Fi channel is selected at installation

Date: 2026-10-29
Status: Accepted

## Context
The original specification fixed channel 1 without site data. Wi-Fi networks in the building may use the same channel.
All ESP-NOW nodes must be on the same channel.

In the espressif/esp-now component (source: `espnow.h`, `espnow.c`):
- `frame_head.channel = ESPNOW_CHANNEL_ALL`: the packet is sent on every channel of the country setting, one after another.
- `CONFIG_ESPNOW_AUTO_RESTORE_CHANNEL`: the original channel is restored after sending.
- There is no channel scan or channel discovery function.

## Decision
- Candidate channels: 1, 6, 11 (non-overlapping).
- At installation the panel runs a site scan with `esp_wifi_scan_start` and selects one candidate.
  Selection criterion: fewest APs; on a tie, the lowest maximum RSSI.
- The selected channel is stored in the panel's NVS.
- The channel is sent encrypted to the apartment module during pairing; the module stores it in NVS.
- The channel cannot be changed after installation. If it must change, all apartments are paired again.
- If no channel is stored in NVS, the panel scans automatically at boot (first installation, after a building reset).
- A building reset also erases the channel and the application key.
- How an unpaired module finds the panel's channel: see ADR 0007.
