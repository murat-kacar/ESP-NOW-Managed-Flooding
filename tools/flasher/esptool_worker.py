"""Run one esptool operation on one device and report it as JSON lines on stdout.

The flasher GUI starts one worker process per device, so devices are flashed
in true parallel. esptool output is captured through its documented custom
logger hook (``esptool.logger.log.set_logger``) instead of parsing console text.

Usage:
    python esptool_worker.py probe --port COM3
    python esptool_worker.py scan --port COM3
    python esptool_worker.py esptool <esptool arguments...>

Event lines (one JSON object per line):
    {"type": "log", "level": "info|note|warn|error|debug", "text": "..."}
    {"type": "progress", "percent": 42.5}
    {"type": "info", "data": {...}}
    {"type": "result", "ok": true}
    {"type": "result", "ok": false, "error": "..."}
"""

import argparse
import json
import sys
from typing import Any

import esptool
from esp_pylib.logger import EspLogBase
from esptool.cmds import detect_chip, detect_flash_size, run_stub
from esptool.logger import log
from rich.text import Text


def emit(event: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


def plain(args: tuple[Any, ...], sep: str = " ") -> str:
    """Join log arguments and strip rich markup used by esptool."""
    return Text.from_markup(sep.join(str(a) for a in args)).plain


class JsonLogger(EspLogBase):
    """esptool logger that forwards every message as a JSON event."""

    def print(self, *args, **kwargs) -> None:
        text = plain(args, kwargs.get("sep", " "))
        if text.strip():
            emit({"type": "log", "level": "info", "text": text})

    def err(self, *args: Any, suggestion: str | None = None) -> None:
        emit({"type": "log", "level": "error", "text": plain(args)})

    def warn(self, *args: Any, suggestion: str | None = None) -> None:
        emit({"type": "log", "level": "warn", "text": plain(args)})

    def note(self, *args: Any) -> None:
        emit({"type": "log", "level": "note", "text": plain(args)})

    def hint(self, *args: Any) -> None:
        emit({"type": "log", "level": "note", "text": plain(args)})

    def debug(self, *args: Any) -> None:
        pass

    def set_verbosity(self, mode: int | str) -> None:
        pass

    def progress_bar(
        self,
        cur_iter: int,
        total_iters: int,
        prefix: str = "",
        suffix: str = "",
        bar_length: int = 30,
    ) -> None:
        if total_iters > 0:
            emit({"type": "progress", "percent": round(100.0 * cur_iter / total_iters, 1)})


def format_mac(mac: tuple[int, ...]) -> str:
    return ":".join(f"{b:02x}" for b in mac)


def probe(port: str) -> dict[str, Any]:
    """Identify the chip on a port, then reset it back into its application."""
    with detect_chip(port) as esp:
        info = {
            "chip": esp.get_chip_description(),
            "mac": format_mac(esp.read_mac()),
        }
        esp.hard_reset()
    return info


def scan(port: str) -> dict[str, Any]:
    """Read-only health check: chip, MAC, flash size and security state."""
    with detect_chip(port) as esp:
        info = {
            "chip": esp.get_chip_description(),
            "mac": format_mac(esp.read_mac()),
            "secure_boot": bool(esp.get_secure_boot_enabled()),
            "flash_encryption": bool(esp.get_flash_encryption_enabled()),
        }
        stub = run_stub(esp)
        info["flash_size"] = detect_flash_size(stub)
        stub.hard_reset()
    return info


def main() -> int:
    argv = sys.argv[1:]
    # "esptool" passes everything after it to esptool unchanged. argparse.REMAINDER is not used:
    # it is a legacy feature that rejects pass-through arguments starting with "-" (e.g. --chip).
    passthrough = argv[1:] if argv[:1] == ["esptool"] else None
    if passthrough is None:
        parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
        sub = parser.add_subparsers(dest="command", required=True)
        for name in ("probe", "scan"):
            sub.add_parser(name).add_argument("--port", required=True)
        sub.add_parser("esptool", help="pass the remaining arguments to esptool")
        args = parser.parse_args(argv)

    log.set_logger(JsonLogger())
    try:
        if passthrough is not None:
            esptool.main(passthrough)
        elif args.command == "probe":
            emit({"type": "info", "data": probe(args.port)})
        else:
            emit({"type": "info", "data": scan(args.port)})
    except SystemExit as e:
        if e.code not in (0, None):
            emit({"type": "result", "ok": False, "error": f"esptool exited with code {e.code}"})
            return 1
    except Exception as e:  # report any esptool or serial failure to the GUI
        emit({"type": "result", "ok": False, "error": f"{type(e).__name__}: {e}"})
        return 1

    emit({"type": "result", "ok": True})
    return 0


if __name__ == "__main__":
    sys.exit(main())
