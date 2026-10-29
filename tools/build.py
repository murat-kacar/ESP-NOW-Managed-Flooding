#!/usr/bin/env python
"""Build firmware and produce one merged image per role (SPEC 8).

Output: builds/<role>_YYYY-MM-DD_HH-MM-SS.bin, flashed at offset 0x0.

Run from an ESP-IDF environment (IDF_PATH set):
    python tools/build.py              # all roles
    python tools/build.py panel        # one role
"""

import argparse
import os
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


def build_role(role: str, timestamp: str) -> Path:
    project_dir = REPO_ROOT / "apps" / role
    output = OUTPUT_DIR / f"{role}_{timestamp}.bin"
    # idf.py has ccache disabled by default; enable it explicitly instead of relying on the shell profile.
    idf_py(project_dir, "--ccache", "build")
    # Raw format pads gaps with 0xFF, so flashing at 0x0 also erases the NVS region.
    idf_py(project_dir, "merge-bin", "-o", str(output))
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("roles", nargs="*", choices=ROLES, default=list(ROLES), help="roles to build")
    args = parser.parse_args()

    OUTPUT_DIR.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    outputs = [build_role(role, timestamp) for role in args.roles]

    for output in outputs:
        print(f"Merged image: {output}")


if __name__ == "__main__":
    main()
