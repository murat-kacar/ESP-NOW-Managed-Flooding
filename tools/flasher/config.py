"""Constants shared by the flasher modules."""

import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BUILDS_DIR = REPO_ROOT / "builds"
RECORDS_FILE = REPO_ROOT / "records" / "devices.csv"

ROLES = ("panel", "apartment")
PROFILE_DEVELOPMENT = "development"
PROFILE_PRODUCTION = "production"

CHIP = "esp32"
FLASH_BAUD = 460800  # idf.py flash default

FACTORY_PARTITION = "fctry"  # per-device factory NVS (ADR 0003)
USER_NVS_PARTITION = "nvs"
FACTORY_NAMESPACE = "factory"

# Kconfig options for code lengths (apps/panel/main/Kconfig.projbuild) and their Kconfig defaults,
# used only when the configuration form is not loaded.
ADMIN_CODE_LENGTH_OPTION = "DB_ADMIN_CODE_LENGTH"
USER_CODE_LENGTH_OPTION = "DB_USER_CODE_LENGTH"
ADMIN_CODE_LENGTH_FALLBACK = 6
USER_CODE_LENGTH_FALLBACK = 4

WINDOW_WIDTH = 1100
WINDOW_HEIGHT = 680
LEFT_COLUMN_WIDTH = 400


def idf_path() -> Path:
    """Return IDF_PATH or raise if the tool is not run from an ESP-IDF environment."""
    value = os.environ.get("IDF_PATH")
    if not value:
        raise RuntimeError("IDF_PATH is not set. Start the flasher from an ESP-IDF environment.")
    return Path(value)


def dev_code(length: int) -> str:
    """Development default code: 1234 for 4 digits, 123456 for 6 digits (ADR 0003 applies to production)."""
    return "1234567890"[:length]


def firmware_elf(role: str) -> Path:
    return REPO_ROOT / "apps" / role / "build" / f"{role}.elf"


def latest_firmware(role: str) -> Path | None:
    """Newest merged image for a role; names sort by their timestamp."""
    images = sorted(BUILDS_DIR.glob(f"{role}_*.bin"))
    return images[-1] if images else None


def image_option(image: Path, name: str) -> str | None:
    """Value of CONFIG_<name> in the sdkconfig fragment saved next to an image, if it was changed for that build."""
    fragment = image.with_suffix(".config")
    if not fragment.is_file():
        return None
    prefix = f"CONFIG_{name}="
    for line in fragment.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line[len(prefix):].strip().strip('"')
    return None
