#!/usr/bin/env python3
"""Keep a pattern table in step with the patterns it was built from.

The table is generated from the game executable (build_pattern_table.py) and committed,
so a build machine without the game cannot regenerate it. It can still tell that it went
stale: the table records a hash of the pattern literals it was built from.

With --regenerate, and a game executable it can find, a stale table is simply rebuilt --
which reuses every entry the new pattern set shares with the old one, so the usual "I
added a pattern" rebuild costs one scan. Without the game, the failure is the old one: a
clear message naming the command to run on a machine that has it.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_pattern_table import (MAGIC, add_exe_arguments, build_table, pattern_set_hash,
                                 read_literals, resolve_exe)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("table", type=Path, help="the pattern table to check")
    ap.add_argument("patterns", type=Path, help="the source file holding the pattern literals")
    ap.add_argument("style", nargs="?", choices=("call", "table"), default="call",
                    help="how the literals appear in the source")
    ap.add_argument("--regenerate", action="store_true",
                    help="rebuild a stale table when the game executable can be found")
    ap.add_argument("--jobs", type=int, default=0, help="parallel scans (default: up to 8)")
    add_exe_arguments(ap)
    args = ap.parse_args()

    current = pattern_set_hash(read_literals(args.patterns, args.style))
    reason = None

    if not args.table.exists():
        reason = "%s is missing" % args.table
    else:
        raw = args.table.read_bytes()
        if len(raw) < 48 or raw[:8] != MAGIC:
            print("%s is not a pattern table" % args.table, file=sys.stderr)
            return 1
        # patternSetHash sits at +16 in every format version (magic 8 + version 4 + count 4).
        if struct.unpack_from("<Q", raw, 16)[0] != current:
            reason = "%s is stale: it was built from a different set of patterns." % args.table

    if reason is None:
        return 0

    exe = resolve_exe(args.exe, args.exe_env, args.steam_app, args.steam_relative) if args.regenerate else None
    if exe is not None and exe.is_file():
        print("%s Regenerating from %s" % (reason, exe))
        return build_table(exe, args.patterns, args.table, args.style, args.jobs)

    print(reason, file=sys.stderr)
    print("Re-run on a machine with the game: python scripts/build_pattern_table.py <game.exe> %s -o %s --style %s"
          % (args.patterns, args.table, args.style), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
