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
FORMAT_VERSION = 2
LITERAL_RE = re.compile(r'(?:get_pattern|get_opcode_address)\s*(?:<[^>]*>)?\s*\(\s*\n?\s*"([0-9A-Fa-f? ]+)"')

# A C string literal, and a run of adjacent literals the compiler concatenates.
CXX_STRING = r'"(?:[^"\\]|\\.)*"'
LITERAL_RUN_RE = re.compile(r"(?:" + CXX_STRING + r"\s*)+")
CXX_STRING_RE = re.compile(CXX_STRING)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")

FNV1_PRIME = 1099511628211
FNV1_BASIS = 14695981039346656037
MASK64 = (1 << 64) - 1


def framework_hash(literal: str, with_nul: bool) -> int:
    """hook::pattern's FNV-1 over the pattern text.

    Which bytes are hashed depends on WHICH CONSTRUCTOR the caller reaches, and the two
    disagree:

      hook::pattern p("48 8B ...");   -> pattern(const char (&)[Len]), Len counts the NUL,
                                         so Initialize() hashes the terminating NUL too.
      const char *pat = ...;          -> pattern(std::string_view), size() excludes the NUL.
      hook::pattern p(pat);

    A table built with the wrong convention hashes to keys nothing ever looks up: every
    pattern silently falls back to a scan. Emitting both keys per pattern costs 16 bytes
    and removes the failure mode entirely.
    """
    h = FNV1_BASIS
    for ch in literal.encode("ascii") + (b"\0" if with_nul else b""):
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


def read_literals(path: Path, style: str = "call"):
    """Collect the pattern literals from a source file, in source order, de-duplicated.

    Two shapes exist in the tree:

      "call"  -- m2o: the literal sits inside a get_pattern()/get_opcode_address() call.
      "table" -- HogwartsMP: the literal sits in a plain `Aob{name, pattern, optional}`
                 aggregate in game_layout.h, so there is no call to anchor on. Instead,
                 take every run of adjacent string literals whose concatenation is nothing
                 but hex bytes, '?' wildcards and spaces. Names never qualify (they carry
                 '/' or '_'), and any false positive is caught immediately because the
                 generator refuses to emit a table with an unmatched pattern.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    out, seen = [], set()

    if style == "call":
        found = [lit.strip() for lit in LITERAL_RE.findall(text)]
    else:
        found = []
        for run in LITERAL_RUN_RE.findall(LINE_COMMENT_RE.sub("", text)):
            # Adjacent literals concatenate with no separator, exactly as the compiler does.
            joined = "".join(part[1:-1] for part in CXX_STRING_RE.findall(run))
            if re.fullmatch(r"[0-9A-Fa-f? ]+", joined) and len(joined.split()) >= 8:
                found.append(joined)

    for lit in found:
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
    opt = e_lfanew + 24

    # ImageBase moves with the optional-header magic: PE32 has a 4-byte BaseOfData at +24
    # and a 4-byte ImageBase at +28, PE32+ drops BaseOfData and puts an 8-byte ImageBase at
    # +24. Reading the PE32 offset unconditionally (as this did) yields the HIGH dword of an
    # x64 base -- 1 for the usual 0x140000000 -- and the client then rejects the table as
    # built for a different image, which is why no x64 project could ever use one.
    magic = struct.unpack_from("<H", raw, opt)[0]
    if magic == 0x20B:  # PE32+
        image_base = struct.unpack_from("<Q", raw, opt + 24)[0]
    elif magic == 0x10B:  # PE32
        image_base = struct.unpack_from("<I", raw, opt + 28)[0]
    else:
        raise SystemExit("unrecognised optional header magic 0x%04X in %s" % (magic, exe))

    size_of_image = struct.unpack_from("<I", raw, opt + 56)[0]
    image = bytearray(size_of_image)
    off = opt + opt_size
    for _ in range(sections):
        _vsize, va, rsize, praw = struct.unpack_from("<IIII", raw, off + 8)
        image[va:va + rsize] = raw[praw:praw + rsize]
        off += 40
    return bytes(image), image_base, size_of_image, len(raw)


def to_regex(literal: str):
    parts = []
    for tok in literal.split():
        parts.append(b"." if tok == "?" else re.escape(bytes([int(tok, 16)])))
    # Wrapped in a lookahead so finditer reports OVERLAPPING matches: pattern::EnsureMatches
    # advances one byte past a hit, so a self-overlapping pattern matches more often at
    # runtime than a plain non-overlapping finditer would report. The counts have to agree
    # or a seeded pattern resolves to a different match set than a scan would.
    return re.compile(b"(?=" + b"".join(parts) + b")", re.DOTALL)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", type=Path, help="the game executable to resolve against")
    ap.add_argument("patterns", type=Path, help="the source file holding the pattern literals")
    ap.add_argument("-o", "--out", type=Path, required=True, help="table to write")
    ap.add_argument("--style", choices=("call", "table"), default="call",
                    help="how the literals appear in the source: inside a get_pattern() call "
                         "(default) or in an Aob aggregate table")
    args = ap.parse_args()

    literals = read_literals(args.patterns, args.style)
    if not literals:
        print("no patterns found in %s" % args.patterns, file=sys.stderr)
        return 1
    image, image_base, size_of_image, file_size = map_image(args.exe)
    print("%s: %d bytes mapped to 0x%X..0x%X" % (args.exe.name, file_size, image_base, image_base + size_of_image))
    print("%d unique patterns" % len(literals))

    entries, missing, ambiguous = [], [], []
    by_hash = {}
    for lit in literals:
        hits = [m.start() for m in to_regex(lit).finditer(image)]
        if not hits:
            missing.append(lit)
            continue
        if len(hits) > 1:
            ambiguous.append((lit, len(hits)))
        # Emit EVERY match, not just the first. The hint map is a multimap and the client
        # replays all of a hash's hints, so seeding the full match set is what makes a seeded
        # lookup agree with a scan -- callers that reject an ambiguous pattern still see the
        # ambiguity, and HogwartsMP's AobNearest still gets both twins to choose between.
        for with_nul in (False, True):
            h = framework_hash(lit, with_nul)
            if by_hash.setdefault(h, lit) != lit:
                print("hash collision: %s and %s" % (lit[:40], by_hash[h][:40]), file=sys.stderr)
                return 1
            entries.extend((h, rva) for rva in hits)

    for lit, n in ambiguous:
        print("ambiguous (%d matches, all recorded): %s" % (n, lit[:70]), file=sys.stderr)
    for lit in missing:
        print("no match: %s" % lit[:70], file=sys.stderr)
    if missing:
        print("refusing to write a table with %d unmatched pattern(s)" % len(missing), file=sys.stderr)
        return 1

    # Ascending rva within a hash: std::multimap preserves insertion order for equal keys,
    # and a scan yields matches in ascending address order, so this keeps get(0) identical.
    entries.sort()

    blob = b"".join(struct.pack("<QII", h, rva, 0) for h, rva in entries)
    header = struct.pack("<8sIIQQIII4x", MAGIC, FORMAT_VERSION, len(entries),
                         pattern_set_hash(literals), image_base, size_of_image,
                         file_size, zlib.crc32(blob) & 0xFFFFFFFF)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(header + blob)
    print("wrote %s: %d entries (%d patterns x2 hash conventions), %d bytes"
          % (args.out, len(entries), len(literals), len(header) + len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
