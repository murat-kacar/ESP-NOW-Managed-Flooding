# 9. Project structure

Date: 2026-10-29
Status: Accepted

## Context
Two roles (outdoor panel, apartment module) need the same communication code.
Development environment: ESP-IDF v6.1 (`C:\esp\v6.1\esp-idf`).

## Decision
The documented ESP-IDF approach: each role is a separate ESP-IDF project; shared code lives in components shared via `EXTRA_COMPONENT_DIRS`.

```
apps/panel/        Outdoor panel project
apps/apartment/    Apartment module project
components/        Shared components (db_net, ...)
docs/              Specification and ADRs
```

- The CMake files match ESP-IDF v6.1 `tools/templates/sample_project` and `examples/get-started/hello_world` (`cmake_minimum_required(VERSION 3.22)`, `MINIMAL_BUILD ON`).
- Wi-Fi init order follows ESP-IDF `examples/wifi/espnow` and the esp-now `examples/get-started` example.
- Dependencies are declared in `idf_component.yml`; `dependencies.lock` is committed.
- Each project has its own `sdkconfig.defaults`.

## Consequences
- Each role is built separately: `idf.py -C apps/panel build`, `idf.py -C apps/apartment build`.
- The deliverable is one file per role: `python tools/build.py` → `builds/<role>_YYYY-MM-DD_HH-MM-SS.bin` (offset 0x0).
  The script only calls the official `idf.py build` and `idf.py merge-bin` commands.
- Every build deletes the generated `sdkconfig` and passes `SDKCONFIG_DEFAULTS` explicitly
  (`sdkconfig.defaults`, plus an optional fragment via `--config`), because ESP-IDF applies defaults only when no `sdkconfig` exists.
  Configuration is therefore defined by files only; changes made with `idf.py menuconfig` are not kept.
- Adjustable parameters are Kconfig options: `components/db_net/Kconfig` and `apps/<role>/main/Kconfig.projbuild` (prefix `CONFIG_DB_`).
- espressif/esp-now 2.5.3 builds with IDF 6.1 (2026-10-29). Requirement: `CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y`
  (off by default in IDF 6.x; every esp-now example enables it).
- First build: application image 0xbca50 bytes; 26 % of the default 1 MB application partition is free.
- ESP-IDF does not support spaces in project paths (ESP-IDF get-started documentation), so the project path must not contain spaces.
