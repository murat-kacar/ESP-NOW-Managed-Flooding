# 4. Secure Boot v2, Flash Encryption and NVS Encryption

Date: 2026-10-29
Status: Accepted

## Context
Flash holds per-device codes (ADR 0003) and espnow_sec keys. Without protection they can be read from flash.
Chip revision of the boards in use: ESP32 v3 (verified by the user with `esptool.py chip_id`).
Espressif documentation: Secure Boot v2 is supported on ESP32 from revision v3.0.

The project is in development and testing; every change must be reversible in this phase.
According to Espressif documentation, even Flash Encryption Development mode performs irreversible eFuse operations:
the key is written once and protected, JTAG is disabled, and encryption can be disabled only 3 times per chip.

## Decision
Two configuration profiles are used (via ESP-IDF `SDKCONFIG_DEFAULTS`):

| | Development (`sdkconfig.defaults`) | Production (`sdkconfig.defaults.prod`) |
|---|---|---|
| `CONFIG_ESP32_REV_MIN` | v3.0 | v3.0 |
| Secure Boot v2 | Off | On (RSA-PSS signed images) |
| Flash Encryption | Off | On, Release mode |
| NVS Encryption | Off | On (flash-encryption-based `nvs_keys`; ESP32 has no HMAC peripheral) |
| eFuse burning | None | On the production line |

## Consequences
- No eFuse is burned on development boards; all changes stay reversible.
- The production profile is validated on dedicated test boards before any eFuse is burned.
- Production can only use ESP32 revision v3.0 or later.
- Secure Boot v2 accepts only signed images, so every production image is signed; the signing key is kept in a
  secure environment. There is no OTA (ADR 0013).
- Flashing a merged image at 0x0 (SPEC section 8) applies to the development profile only; the production flashing flow will be defined separately.
