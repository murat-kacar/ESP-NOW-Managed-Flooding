# 3. Per-device default codes

Date: 2026-10-29
Status: Accepted

## Context
The original specification used the same factory codes on every panel (`1234`, `123456`).
ETSI EN 303 645 provision 5.1-1 forbids universal default passwords.

## Decision
- Each panel gets a random user code (4 digits) and admin code (6 digits), generated in production and printed on a label.
- The codes are written with the ESP-IDF Manufacturing Utility (`mfg_gen.py`) to a separate, read-only factory NVS partition.
- Codes changed by the user are stored in the normal NVS partition.
- Panel code reset (GPIO 19) erases only the codes in normal NVS; the panel falls back to the label codes in the factory partition.

## Alternative considered
Setting the codes on first use: no extra production step, but the panel stays in setup mode if setup is not completed.

## Consequences
- Production needs a code generation and label printing step.
- Flash Encryption and NVS Encryption are required so the factory codes cannot be read from flash (ADR 0004).
