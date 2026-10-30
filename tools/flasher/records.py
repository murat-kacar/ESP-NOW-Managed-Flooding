"""Append one row per device operation to the device record CSV.

The file contains plain-text codes: it is git-ignored and must be stored with restricted access.
"""

import csv
from datetime import datetime

from .config import RECORDS_FILE

FIELDS = ("timestamp", "action", "profile", "role", "port", "chip", "mac", "admin_code", "user_code", "firmware")


def append(**row: str) -> None:
    RECORDS_FILE.parent.mkdir(parents=True, exist_ok=True)
    is_new = not RECORDS_FILE.exists()
    row.setdefault("timestamp", datetime.now().isoformat(timespec="seconds"))
    with RECORDS_FILE.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS, restval="")
        if is_new:
            writer.writeheader()
        writer.writerow(row)
