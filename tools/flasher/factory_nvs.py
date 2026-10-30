"""Per-device factory codes and the CSV input for the ESP-IDF NVS partition generator."""

import secrets
from dataclasses import dataclass
from pathlib import Path

from .config import FACTORY_NAMESPACE


@dataclass(frozen=True)
class CodeLengths:
    admin: int
    user: int


@dataclass(frozen=True)
class FactoryCodes:
    admin_code: str
    user_code: str

    def validate(self, lengths: CodeLengths) -> None:
        for label, value, length in (
            ("Admin code", self.admin_code, lengths.admin),
            ("User code", self.user_code, lengths.user),
        ):
            if len(value) != length or not value.isdigit():
                raise ValueError(f"{label} must be exactly {length} digits (as configured in the firmware).")


def random_code(length: int) -> str:
    """Digits from the OS cryptographic random source."""
    return "".join(secrets.choice("0123456789") for _ in range(length))


def write_csv(codes: FactoryCodes, path: Path) -> None:
    """Write the nvs_partition_gen input CSV (key,type,encoding,value)."""
    path.write_text(
        "key,type,encoding,value\n"
        f"{FACTORY_NAMESPACE},namespace,,\n"
        f"admin_code,data,string,{codes.admin_code}\n"
        f"user_code,data,string,{codes.user_code}\n",
        encoding="utf-8",
    )
