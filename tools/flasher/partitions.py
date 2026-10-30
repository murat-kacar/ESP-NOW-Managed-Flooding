"""Read partition offsets from a role's partitions.csv with ESP-IDF's own parser."""

import sys
from dataclasses import dataclass

from .config import REPO_ROOT, idf_path


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int
    size: int


def find_partition(role: str, name: str) -> Partition | None:
    """Return the named partition, or None if the role has no custom table or no such partition."""
    table_file = REPO_ROOT / "apps" / role / "partitions.csv"
    if not table_file.exists():
        return None

    parser_dir = str(idf_path() / "components" / "partition_table")
    if parser_dir not in sys.path:
        sys.path.insert(0, parser_dir)
    import gen_esp32part  # ESP-IDF partition table tool

    table = gen_esp32part.PartitionTable.from_csv(table_file.read_text())
    entry = table.find_by_name(name)
    if entry is None:
        return None
    return Partition(name=entry.name, offset=entry.offset, size=entry.size)
