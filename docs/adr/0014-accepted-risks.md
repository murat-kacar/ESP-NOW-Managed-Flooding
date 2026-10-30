# 14. Accepted risks from the code review

Date: 2026-10-31
Status: Accepted

## Context
A review of the firmware and the flasher (2026-10-30) found the risks below. For each one, the fix would cost more
in complexity than the risk is worth for this product. Risk acceptance is recorded with its reason and with the
condition that would reopen it (IEC 62443 / ISO 27005 risk treatment). The missing PoP is accepted in ADR 0007.

## Decision
### 1. One application key for the whole building
espnow_sec gives the same application key to every module, and the message tag (ADR 0012) is derived from it. A
device holding the key can produce valid messages for any source MAC. Deleting an apartment (SPEC 5.2) does not
revoke the removed module's key.
- Accepted because: in production, Secure Boot v2 and Flash Encryption in Release mode (ADR 0004) let a removed
  module run only signed firmware and keep its key unreadable. Abusing the key then needs a hardware attack on the
  chip.
- Remedy if needed: a building reset replaces the key; every module is paired again.
- Reopen if: per-device revocation is required. This would need per-device keys and a key refresh procedure (as in
  BLE Mesh), in a new ADR.

### 2. The wrong-code lockout is kept in RAM
The failure count and lockout level (SPEC 4.1) reset when the panel restarts, so cutting the panel's power repeatedly
would allow more attempts.
- Accepted because: the panel's power supply is not reachable from outside the building. Persisting the count would
  cost a flash write for every wrong code.
- Reopen if: an installation exposes the panel's power supply.

### 3. The panel is busy during a pairing window
Pairing runs in the panel's application task for up to `DB_PAIRING_WINDOW_S`. During that time the keypad is not
processed and no apartment can be rung. Keys pressed meanwhile wait in the event queue, or are dropped when it is full.
- Accepted because: pairing is rare and is done by the installer standing at the panel.
- Reopen if: pairing has to happen while the building is in normal use.

### 4. The flasher takes code lengths from its configuration form
A merged image does not carry its configuration. The flasher validates factory codes against the code lengths shown
in its form, not against the image being flashed. Flashing an older image built with other code lengths writes
factory codes that the panel rejects.
- Accepted because: the same person builds and flashes, in the development profile. Recovery is to flash the factory
  NVS again with codes of the right length.
- Reopen if: images are flashed by someone other than the person who built them (production line).

## Consequences
- These four points are known and deliberate, not open defects.
- Each one is reviewed again when its reopen condition occurs, and in any case when the production profile is
  defined (ADR 0004).
