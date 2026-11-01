# ESP-NOW Doorbell

Apartment doorbell and door access system on ESP32, over an ESP-NOW managed-flooding network. The system is
closed: devices talk only to each other, never to an access point, the internet or a cloud service, and there is no
OTA ([ADR 0013](docs/adr/0013-closed-system-no-ota.md)).

| Role | Where | Does |
| :--- | :--- | :--- |
| **Panel** | Building entrance, one per building | Keypad, door lock relay, buzzer; rings apartments, opens the door with a code |
| **Apartment module** | One per apartment | Bell relay, door-open button; reports its health to the panel |

Messages reach distant floors through the other modules (espressif/esp-now forwarding). Every application message
is authenticated (HMAC-SHA256) and replay-protected; only the channel discovery frames used during pairing are not.
Pairing is done next to the panel.

Status: development profile (no Secure Boot or Flash Encryption yet), bench and building tests in progress.

## Hardware
NodeMCU-32S (ESP32-WROOM-32, chip revision v3 or later). GPIO map: [SPEC section 2](docs/SPEC.md).

## Repository layout
| Path | Content |
| :--- | :--- |
| `apps/panel`, `apps/apartment` | The two ESP-IDF applications |
| `components/` | Shared components: network (`db_net`), protocol (`db_proto`), NVS (`db_nvs`), status LED (`db_status_led`), watchdog (`db_wdt`) |
| `tools/flasher` | Build and flashing GUI (tkinter): configuration form, parallel flashing, factory codes |
| `tools/build.py` | Builds one merged image per role into `builds/` |
| `docs/SPEC.md`, `docs/adr/` | Specification and architecture decision records |
| `docs/tr/` | User manual and configuration guide (Turkish) |

## Build and flash
Requirements: ESP-IDF v6.1 installed with the ESP-IDF Installation Manager (EIM) on Windows.

1. Run `flasher.bat`. It activates ESP-IDF and starts the GUI (`python -m tools.flasher`).
2. Choose the role, adjust the configuration if needed, and build. The image is written to
   `builds/<role>_<timestamp>.bin`.
3. Connect the boards, press **Detect**, pick the role and image per device, and flash. Panels also get their
   per-device factory codes; the flashing history is kept in `records/history.csv` (git-ignored, plain-text codes).

Without the GUI, from an ESP-IDF shell:

```bash
python tools/build.py panel
```

## Continuous integration
Every push to `main` builds both roles on GitHub Actions in the `espressif/idf:v6.1` Docker image with
`tools/build.py` (`.github/workflows/build.yml`, [ADR 0018](docs/adr/0018-continuous-integration.md)). The merged
images are attached to each run as artifacts.

## Documentation
- [SPEC.md](docs/SPEC.md): behaviour, protocol, LED states, items still to verify
- [ADRs](docs/adr/): why each design decision was made
- [Kullanım Kılavuzu](docs/tr/kullanim-kilavuzu.md) and [Yapılandırma Ayarları](docs/tr/yapilandirma-ayarlari.md)
