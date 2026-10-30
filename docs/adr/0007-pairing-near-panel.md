# 7. Pairing is done next to the panel

Date: 2026-10-29
Status: Accepted

## Context
From the espressif/esp-now source code:
- The component only forwards packets with `broadcast = true`.
- In the espnow_sec handshake, every packet after Command 0 is unicast (`broadcast = false`), so it is not forwarded.
- `CONFIG_ESPNOW_SEC_SEND_FORWARD_TTL` defaults to 0.
- Initiator and responder never change channel; both must be on the same channel.
- The responder accepts the first initiator that starts a handshake and ignores new handshakes once configured.

Result: key exchange only works within the panel's direct radio range. (To be verified by a bench test.)

## Decision
- The installer brings the module next to the panel, pairs it, then installs it in the apartment.
- espnow_sec is used unmodified: the panel is the initiator, the apartment module is the responder.
- Channel discovery: the unpaired module listens on channels 1, 6 and 11 in turn (`esp_wifi_set_channel`, in application code).
  The panel stays on its own channel and scans with `espnow_sec_initiator_scan` during the pairing window.
  When the handshake completes, the module stores its current channel in NVS. Dwell time per channel will be set by a bench test.
- No PoP is used. If the component requires a PoP value, the same non-secret fixed value is used on all devices
  (NULL support to be verified). Protection relies on physical proximity and the 20 s pairing window.
- If the scan finds more than one unpaired module, pairing is cancelled and the panel shows an error.

## Alternatives considered
- Modifying the component for a multi-hop handshake: deviates from official code.
- Writing a custom key exchange: custom cryptography, rejected.
- Per-device PoP printed on a label (Espressif Wi-Fi provisioning approach): rejected by user preference.

## Consequences
- Pairing requires physical proximity; a rogue device cannot be paired remotely.
- The in-apartment pairing flow of the original specification (section 3.3) is replaced.

## Accepted risk
- While the pairing window is open, any unpaired module within the panel's range could be paired.
  Mitigation: pairing is cancelled when more than one unpaired module is found.
