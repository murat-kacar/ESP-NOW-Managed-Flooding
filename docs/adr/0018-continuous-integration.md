# 18. Continuous integration

Date: 2026-11-01
Status: Accepted

## Context
Firmware changes were committed without a build: local builds are slow, so they ran only when the owner built from
the flasher GUI, sometimes many commits later. Compile errors were found late and far from the change that caused
them.

Embedded projects test in layers: a build on every change, static analysis, unit tests on the host, and tests on
the hardware (hardware in the loop). ESP-IDF supports each of them: Espressif's ESP-IDF Docker images and
`espressif/esp-idf-ci-action`, `idf.py clang-check`, the Linux target with Unity and CMock, and `pytest-embedded`.

## Decision
- **Now: build on every push** (`.github/workflows/build.yml`). GitHub Actions builds both roles in parallel in the
  `espressif/idf:v6.1` image, the same ESP-IDF version as the local installation, with `tools/build.py`. The build
  therefore also runs the Kconfig usage check, and a failed check fails the build. The merged images are kept as
  workflow artifacts for 14 days.
- Third-party actions are pinned to commit SHAs, as GitHub's security hardening guide recommends.
- **Later, in this order:** static analysis (`idf.py clang-check`, `ruff` for the tools); host unit tests of logic
  that needs no hardware (keypad parser, replay and request-id checks, codes, lockout), which first needs that logic
  separated from the radio and NVS calls; hardware-in-the-loop tests with `pytest-embedded` on a self-hosted runner
  with boards attached.

## Consequences
- A compile error shows on GitHub minutes after the push.
- The owner still flashes images built locally by the GUI; the CI images are for checking, and can be downloaded if
  needed.
- Private repository: the runs use the account's included GitHub Actions minutes.
