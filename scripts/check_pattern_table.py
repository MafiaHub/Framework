#!/usr/bin/env python3
"""Fail the build when a pattern table no longer matches the patterns it was built from.

The table is generated on a machine that has the game (build_pattern_table.py) and is
committed, so a build machine without the game cannot regenerate it. It can still tell
that it went stale: the table records a hash of the pattern literals it was built from.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_pattern_table import read_literals, pattern_set_hash, MAGIC


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print("usage: check_pattern_table.py <table> <patterns source> [call|table]", file=sys.stderr)
        return 2
    table, patterns = Path(sys.argv[1]), Path(sys.argv[2])
    style = sys.argv[3] if len(sys.argv) == 4 else "call"
    if not table.exists():
        print("%s is missing; run scripts/build_pattern_table.py <game.exe> %s -o %s"
              % (table, patterns, table), file=sys.stderr)
        return 1

    raw = table.read_bytes()
    if len(raw) < 48 or raw[:8] != MAGIC:
        print("%s is not a pattern table" % table, file=sys.stderr)
        return 1

    # patternSetHash sits at +16 in every format version (magic 8 + version 4 + count 4).
    stored = struct.unpack_from("<Q", raw, 16)[0]
    current = pattern_set_hash(read_literals(patterns, style))
    if stored != current:
        print("%s is stale: it was built from a different set of patterns." % table, file=sys.stderr)
        print("Re-run: python scripts/build_pattern_table.py <game.exe> %s -o %s --style %s"
              % (patterns, table, style), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
