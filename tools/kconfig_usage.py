#!/usr/bin/env python
"""Check that project Kconfig options and the firmware sources reference each other.

Modelled on the Linux kernel's scripts/checkkconfigsymbols.py:
- unused option: a CONFIG_DB_* option defined in Kconfig but referenced by no source file
  (its value set in the GUI cannot reach the device); reported as a warning.
- undefined reference: a CONFIG_DB_* name used in a source file but defined in no Kconfig file
  (in C, an undefined macro in #if silently evaluates to 0); reported as an error.
A choice counts as used when any of its options is referenced.

Run from an ESP-IDF environment:
    python tools/kconfig_usage.py            # all roles
    python tools/kconfig_usage.py panel      # one role
"""

import argparse
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from esp_kconfiglib import core as kc

REPO_ROOT = Path(__file__).resolve().parent.parent
ROLES = ("panel", "apartment")
PREFIX = "DB_"
REFERENCE = re.compile(r"\bCONFIG_(" + PREFIX + r"[A-Z0-9_]+)\b")
SOURCE_SUFFIXES = (".c", ".h")


@dataclass
class Report:
    role: str
    options: int
    unused: list[str]
    undefined: dict[str, list[str]]  # name -> files


def kconfig_files(role: str) -> list[Path]:
    return sorted((REPO_ROOT / "components").glob("*/Kconfig")) + [
        REPO_ROOT / "apps" / role / "main" / "Kconfig.projbuild"
    ]


def source_files(role: str) -> list[Path]:
    roots = [REPO_ROOT / "apps" / role / "main", REPO_ROOT / "components"]
    return sorted(p for root in roots for p in root.rglob("*") if p.suffix in SOURCE_SUFFIXES and p.is_file())


def load_options(role: str) -> tuple[set[str], dict[str, list[str]]]:
    """Return (option names, choice name -> member names) for the project Kconfig files of a role."""
    top = 'mainmenu "Doorbell"\n' + "".join(f'source "{p.as_posix()}"\n' for p in kconfig_files(role))
    fd, top_file = tempfile.mkstemp(prefix="Kconfig-", text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(top)
        kconfig = kc.Kconfig(top_file, warn=False, info=False)
    finally:
        os.unlink(top_file)
    names = {name for name in kconfig.syms if name.startswith(PREFIX)}
    choices = {
        choice.name: [sym.name for sym in choice.syms]
        for choice in kconfig.named_choices.values()
        if choice.name.startswith(PREFIX)
    }
    return names, choices


def check(role: str) -> Report:
    names, choices = load_options(role)
    references: dict[str, list[str]] = {}
    for path in source_files(role):
        for name in set(REFERENCE.findall(path.read_text(encoding="utf-8", errors="replace"))):
            references.setdefault(name, []).append(path.relative_to(REPO_ROOT).as_posix())

    members = {member for group in choices.values() for member in group}
    unused = sorted(name for name in names - members if name not in references)
    unused += sorted(choice for choice, group in choices.items() if not any(m in references for m in group))
    undefined = {name: files for name, files in sorted(references.items()) if name not in names}
    option_count = len(names - members) + len(choices)
    return Report(role=role, options=option_count, unused=sorted(unused), undefined=undefined)


def print_report(report: Report) -> None:
    used = report.options - len(report.unused)
    print(f"Kconfig usage ({report.role}): {used}/{report.options} options are referenced by the firmware.")
    if report.unused:
        print(f"  Warning: {len(report.unused)} option(s) have no effect on the firmware yet:")
        for name in report.unused:
            print(f"    CONFIG_{name}")
    for name, files in report.undefined.items():
        print(f"  Error: CONFIG_{name} is used in {', '.join(files)} but defined in no Kconfig file.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("roles", nargs="*", choices=ROLES, default=list(ROLES))
    args = parser.parse_args()
    failed = False
    for role in args.roles:
        report = check(role)
        print_report(report)
        failed |= bool(report.undefined)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
