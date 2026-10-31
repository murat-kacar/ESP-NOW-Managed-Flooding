"""Device and build operations. Each runs in the job thread and uses only official tools:
esptool, ESP-IDF parttool.py, esp_idf_nvs_partition_gen and tools/build.py.
"""

import sys
import tempfile
from pathlib import Path
from typing import Any

from . import records
from .config import (
    CHIP, FACTORY_PARTITION, FLASH_BAUD, REPO_ROOT, USER_NVS_PARTITION, idf_path,
)
from .factory_nvs import FactoryCodes, write_csv
from .jobs import JobError, JobRunner
from .partitions import Partition, find_partition


def _esptool(port: str, *args: str) -> list[str]:
    return ["esptool", "--chip", CHIP, "--port", port, "--baud", str(FLASH_BAUD), *args]


def _factory_partition(role: str) -> Partition:
    partition = find_partition(role, FACTORY_PARTITION)
    if partition is None:
        raise JobError(f"apps/{role}/partitions.csv has no '{FACTORY_PARTITION}' partition")
    return partition


async def _generate_factory_nvs(runner: JobRunner, port: str, codes: FactoryCodes, size: int, workdir: Path) -> Path:
    csv_file = workdir / "factory_nvs.csv"
    bin_file = workdir / "factory_nvs.bin"
    write_csv(codes, csv_file)
    await runner.command(
        port, sys.executable, "-m", "esp_idf_nvs_partition_gen", "generate", str(csv_file), str(bin_file), str(size),
    )
    return bin_file


async def probe(runner: JobRunner, port: str) -> dict[str, Any]:
    info = await runner.worker(port, "probe", "--port", port)
    runner.post(port, "info", info)
    return info or {}


async def _probe_writable(runner: JobRunner, port: str) -> dict[str, Any]:
    """Probe before every write: a device with secure boot or flash encryption on is not written over serial."""
    device = await probe(runner, port)
    if device.get("secure_boot") or device.get("flash_encryption"):
        raise JobError("secure boot or flash encryption is enabled on this device: serial writes are refused")
    return device


async def scan(runner: JobRunner, port: str) -> None:
    info = await runner.worker(port, "scan", "--port", port)
    runner.post(port, "info", info)


async def flash(
    runner: JobRunner, port: str, role: str, profile: str, firmware: Path, codes: FactoryCodes | None,
) -> None:
    """Write the merged image at 0x0 and, for roles with factory data, the per-device factory NVS."""
    device = await _probe_writable(runner, port)
    # The generated factory NVS image is temporary; only the history row is kept.
    with tempfile.TemporaryDirectory(prefix="fctry-") as tmp:
        files = ["0x0", str(firmware)]
        if codes is not None:
            partition = _factory_partition(role)
            nvs_bin = await _generate_factory_nvs(runner, port, codes, partition.size, Path(tmp))
            files += [hex(partition.offset), str(nvs_bin)]
        await runner.worker(port, *_esptool(port, "write-flash", *files))
    if codes is not None:
        _record("flash", port, role, profile, device, codes, firmware.name)


async def flash_factory_nvs(runner: JobRunner, port: str, role: str, profile: str, codes: FactoryCodes) -> None:
    """Rewrite only the factory NVS partition (codes)."""
    device = await _probe_writable(runner, port)
    partition = _factory_partition(role)
    with tempfile.TemporaryDirectory(prefix="fctry-") as tmp:
        nvs_bin = await _generate_factory_nvs(runner, port, codes, partition.size, Path(tmp))
        await runner.worker(port, *_esptool(port, "write-flash", hex(partition.offset), str(nvs_bin)))
    _record("flash_nvs", port, role, profile, device, codes, "")


async def erase_flash(runner: JobRunner, port: str) -> None:
    await _probe_writable(runner, port)
    await runner.worker(port, *_esptool(port, "erase-flash"))


async def factory_reset(runner: JobRunner, port: str) -> None:
    """Erase the user NVS partition; the factory partition is kept."""
    await _probe_writable(runner, port)
    parttool = idf_path() / "components" / "partition_table" / "parttool.py"
    await runner.command(
        port, sys.executable, str(parttool), "--port", port,
        "erase_partition", "--partition-name", USER_NVS_PARTITION,
    )


async def reconfigure(runner: JobRunner, role: str) -> None:
    """Run `idf.py reconfigure` so build/config.env lists the current Kconfig files (no compilation)."""
    await runner.command(
        "config", sys.executable, str(idf_path() / "tools" / "idf.py"), "-C", str(REPO_ROOT / "apps" / role),
        "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults", "reconfigure",
    )


async def build(runner: JobRunner, role: str, sdkconfig_fragment: str) -> None:
    """Build one role; a non-empty fragment is passed to build.py --config. The only output is the .bin image."""
    cmd = [sys.executable, str(REPO_ROOT / "tools" / "build.py"), role]
    if not sdkconfig_fragment:
        await runner.command("build", *cmd)
        return
    with tempfile.TemporaryDirectory(prefix="config-") as tmp:
        fragment_file = Path(tmp) / "sdkconfig.fragment"
        fragment_file.write_text(sdkconfig_fragment, encoding="utf-8")
        await runner.command("build", *cmd, "--config", str(fragment_file))


def _record(
    action: str, port: str, role: str, profile: str, device: dict[str, Any], codes: FactoryCodes, firmware: str,
) -> None:
    """History row for a factory NVS write (records/history.csv)."""
    records.append(
        action=action,
        profile=profile,
        role=role,
        port=port,
        chip=device.get("chip", ""),
        mac=device.get("mac", ""),
        admin_code=codes.admin_code,
        user_code=codes.user_code,
        firmware=firmware,
    )
