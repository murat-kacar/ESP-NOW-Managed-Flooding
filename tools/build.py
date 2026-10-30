#!/usr/bin/env python
"""Build firmware and produce one merged image per role (SPEC 8).

Output: builds/<role>_YYYY-MM-DD_HH-MM-SS.bin, flashed at offset 0x0.
With --config, the fragment is saved next to the image as builds/<role>_YYYY-MM-DD_HH-MM-SS.config.

Every build regenerates sdkconfig from sdkconfig.defaults (+ the optional fragment),
so an image is always defined by files in the repository and in builds/.

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

REPO_ROOT = Path(__file__).resolve().parent.parent
ROLES = ("panel", "apartment")
OUTPUT_DIR = REPO_ROOT / "builds"


def idf_py(project_dir: Path, *args: str) -> None:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        sys.exit("IDF_PATH is not set. Run this script from an ESP-IDF environment.")
    cmd = [sys.executable, str(Path(idf_path) / "tools" / "idf.py"), "-C", str(project_dir), *args]
    subprocess.run(cmd, check=True)


def build_role(role: str, timestamp: str, config: Path | None) -> Path:
    project_dir = REPO_ROOT / "apps" / role
    output = OUTPUT_DIR / f"{role}_{timestamp}.bin"

    defaults = ["sdkconfig.defaults"]
    if config is not None:
        saved_config = OUTPUT_DIR / f"{role}_{timestamp}.config"
        shutil.copyfile(config, saved_config)
        defaults.append(saved_config.as_posix())

    # ESP-IDF applies SDKCONFIG_DEFAULTS only when sdkconfig does not exist, so remove the generated file.
    (project_dir / "sdkconfig").unlink(missing_ok=True)
    # idf.py has ccache disabled by default; enable it explicitly instead of relying on the shell profile.
    idf_py(project_dir, "--ccache", "-D", f"SDKCONFIG_DEFAULTS={';'.join(defaults)}", "build")
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
