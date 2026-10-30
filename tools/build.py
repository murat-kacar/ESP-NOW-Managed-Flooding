#!/usr/bin/env python
"""Build firmware and produce one merged image per role (SPEC 8).

Output: builds/<role>_YYYY-MM-DD_HH-MM-SS.bin, flashed at offset 0x0. The image is the only output;
every configuration value is compiled into it.

Configuration input: apps/<role>/sdkconfig.defaults, plus an optional sdkconfig fragment (--config)
with values changed in the flasher GUI. The fragment is copied into the build directory
(apps/<role>/build/sdkconfig.fragment) as an intermediate file. Every build regenerates sdkconfig
from these inputs.

Run from an ESP-IDF environment (IDF_PATH set):
    python tools/build.py                              # all roles
    python tools/build.py panel                        # one role
    python tools/build.py panel --config custom.cfg    # one role with an sdkconfig fragment
"""

import argparse
import os
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import kconfig_usage  # tools/kconfig_usage.py

REPO_ROOT = Path(__file__).resolve().parent.parent
ROLES = ("panel", "apartment")
OUTPUT_DIR = REPO_ROOT / "builds"
FRAGMENT_NAME = "sdkconfig.fragment"


def idf_py(project_dir: Path, *args: str) -> None:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        sys.exit("IDF_PATH is not set. Run this script from an ESP-IDF environment.")
    cmd = [sys.executable, str(Path(idf_path) / "tools" / "idf.py"), "-C", str(project_dir), *args]
    subprocess.run(cmd, check=True)


def parse_sdkconfig(path: Path) -> dict[str, str]:
    """Map option name to value from sdkconfig syntax; '# CONFIG_X is not set' maps to 'n'."""
    values = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2:-len(" is not set")]] = "n"
        elif line.startswith("CONFIG_") and "=" in line:
            name, value = line.split("=", 1)
            values[name] = value
    return values


def verify_fragment_applied(project_dir: Path, fragment: Path) -> None:
    """Fail if a requested value is not in the generated sdkconfig.

    kconfgen drops values that are invalid or belong to options hidden by their dependencies,
    with only a warning; this check turns that into a build error.
    """
    requested = parse_sdkconfig(fragment)
    generated = parse_sdkconfig(project_dir / "sdkconfig")
    mismatches = [
        f"  {name}: requested {value}, got {generated.get(name, '<missing>')}"
        for name, value in requested.items()
        if generated.get(name) != value
    ]
    if mismatches:
        sys.exit("Configuration values were not applied by ESP-IDF:\n" + "\n".join(mismatches))
    print(f"Configuration check: all {len(requested)} changed value(s) are in the generated sdkconfig.")


def build_role(role: str, timestamp: str, config: Path | None) -> Path:
    # Fail before compiling if the sources use a CONFIG_DB_* name that no Kconfig file defines.
    report = kconfig_usage.check(role)
    kconfig_usage.print_report(report)
    if report.undefined:
        sys.exit("Undefined Kconfig references; see the errors above.")

    project_dir = REPO_ROOT / "apps" / role
    build_dir = project_dir / "build"
    output = OUTPUT_DIR / f"{role}_{timestamp}.bin"

    defaults = ["sdkconfig.defaults"]
    fragment = build_dir / FRAGMENT_NAME
    fragment.unlink(missing_ok=True)
    if config is not None:
        build_dir.mkdir(exist_ok=True)
        shutil.copyfile(config, fragment)
        defaults.append(fragment.as_posix())

    # ESP-IDF applies SDKCONFIG_DEFAULTS only when sdkconfig does not exist, so remove the generated file.
    (project_dir / "sdkconfig").unlink(missing_ok=True)
    # idf.py has ccache disabled by default; enable it explicitly instead of relying on the shell profile.
    idf_py(project_dir, "--ccache", "-D", f"SDKCONFIG_DEFAULTS={';'.join(defaults)}", "build")
    if config is not None:
        verify_fragment_applied(project_dir, fragment)
    # Raw format pads gaps with 0xFF, so flashing at 0x0 also erases the NVS region.
    idf_py(project_dir, "merge-bin", "-o", str(output))
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("roles", nargs="*", choices=ROLES, default=list(ROLES), help="roles to build")
    parser.add_argument("--config", type=Path, help="sdkconfig fragment applied after sdkconfig.defaults")
    args = parser.parse_args()
    if args.config is not None:
        if len(args.roles) != 1:
            parser.error("--config needs exactly one role")
        if not args.config.is_file():
            parser.error(f"config file not found: {args.config}")

    OUTPUT_DIR.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    outputs = [build_role(role, timestamp, args.config) for role in args.roles]

    for output in outputs:
        print(f"Merged image: {output}")


if __name__ == "__main__":
    main()
