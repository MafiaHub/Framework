#!/usr/bin/env python3
"""Resolve every SDK byte pattern against a game executable and emit m2o.patterns.

A client normally finds each pattern by scanning the whole game image, which for a set of
a thousand-odd patterns costs seconds. This table lets it look the address
up instead. The client still verifies the bytes at every address it is given and falls
back to scanning for anything that does not match, so a table built against a different
game build degrades to the old behaviour rather than misbehaving.

Usage:
    python scripts/build_pattern_table.py <game.exe> <patterns.cpp> -o <project>/data/game.patterns
"""

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"FWPATTBL"
FORMAT_VERSION = 1
LITERAL_RE = re.compile(r'(?:get_pattern|get_opcode_address)\s*(?:<[^>]*>)?\s*\(\s*\n?\s*"([0-9A-Fa-f? ]+)"')

FNV1_PRIME = 1099511628211
FNV1_BASIS = 14695981039346656037
MASK64 = (1 << 64) - 1


def framework_hash(literal: str) -> int:
    """hook::pattern's FNV-1. String literals hash with their terminating NUL."""
    h = FNV1_BASIS
    for ch in literal.encode("ascii") + b"\0":
        h = (h * FNV1_PRIME) & MASK64
        h ^= ch
    return h


def pattern_set_hash(literals) -> int:
    """FNV-1a over the sorted literals; lets the build detect a stale table without the game."""
    h = FNV1_BASIS
    for ch in "\n".join(sorted(literals)).encode("ascii"):
        h ^= ch
        h = (h * FNV1_PRIME) & MASK64
    return h


def read_literals(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    out, seen = [], set()
    for lit in LITERAL_RE.findall(text):
        lit = lit.strip()
        if lit not in seen:
            seen.add(lit)
            out.append(lit)
    return out


def map_image(exe: Path):
    """Lay the PE out the way the loader does, so offsets match runtime RVAs."""
    raw = exe.read_bytes()
    e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
    sections = struct.unpack_from("<H", raw, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", raw, e_lfanew + 20)[0]
    image_base = struct.unpack_from("<I", raw, e_lfanew + 24 + 28)[0]
    size_of_image = struct.unpack_from("<I", raw, e_lfanew + 24 + 56)[0]
    image = bytearray(size_of_image)
    off = e_lfanew + 24 + opt_size
    for _ in range(sections):
        _vsize, va, rsize, praw = struct.unpack_from("<IIII", raw, off + 8)
        image[va:va + rsize] = raw[praw:praw + rsize]
        off += 40
    return bytes(image), image_base, size_of_image, len(raw)


def to_regex(literal: str):
    parts = []
    for tok in literal.split():
        parts.append(b"." if tok == "?" else re.escape(bytes([int(tok, 16)])))
    return re.compile(b"".join(parts), re.DOTALL)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", type=Path, help="the game executable to resolve against")
    ap.add_argument("patterns", type=Path, help="the source file holding the get_pattern literals")
    ap.add_argument("-o", "--out", type=Path, required=True, help="table to write")
    args = ap.parse_args()

    literals = read_literals(args.patterns)
    if not literals:
        print("no patterns found in %s" % args.patterns, file=sys.stderr)
        return 1
    image, image_base, size_of_image, file_size = map_image(args.exe)
    print("%s: %d bytes mapped to 0x%X..0x%X" % (args.exe.name, file_size, image_base, image_base + size_of_image))
    print("%d unique patterns" % len(literals))

    entries, missing, ambiguous = [], [], []
    for lit in literals:
        hits = [m.start() for m in to_regex(lit).finditer(image)]
        if not hits:
            missing.append(lit)
            continue
        if len(hits) > 1:
            ambiguous.append((lit, len(hits)))
        entries.append((framework_hash(lit), hits[0]))

    for lit, n in ambiguous:
        print("ambiguous (%d matches): %s" % (n, lit[:70]), file=sys.stderr)
    for lit in missing:
        print("no match: %s" % lit[:70], file=sys.stderr)
    if missing or ambiguous:
        print("refusing to write a table with %d unmatched and %d ambiguous patterns"
              % (len(missing), len(ambiguous)), file=sys.stderr)
        return 1

    entries.sort()
    if len(set(h for h, _ in entries)) != len(entries):
        print("hash collision between two patterns", file=sys.stderr)
        return 1

    blob = b"".join(struct.pack("<QII", h, rva, 0) for h, rva in entries)
    header = struct.pack("<8sIIQIIII8x", MAGIC, FORMAT_VERSION, len(entries),
                         pattern_set_hash(literals), size_of_image, image_base,
                         file_size, zlib.crc32(blob) & 0xFFFFFFFF)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(header + blob)
    print("wrote %s: %d entries, %d bytes" % (args.out, len(entries), len(header) + len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
