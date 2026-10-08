#!/usr/bin/env python3
"""Resolve every SDK byte pattern against a game executable and emit <project>.patterns.

A client normally finds each pattern by scanning the whole game image, which for a set of
a thousand-odd patterns costs seconds. This table lets it look the address
up instead. The client still verifies the bytes at every address it is given and falls
back to scanning for anything that does not match, so a table built against a different
game build degrades to the old behaviour rather than misbehaving.

Three things keep this fast enough to run from a build:

  * Each pattern is located with bytes.find() over its longest wildcard-free run, then
    verified at the candidate. The obvious spelling -- a regex per pattern -- is ~23x
    slower on a large image, because a pattern wrapped in the lookahead that overlapping
    matches used to need loses CPython's literal-prefix search and steps the regex VM at
    every offset in the image. find() is memmem, and restarting it at hit+1 reproduces
    the overlapping match set exactly.
  * Patterns are scanned across a process pool, since the work is one independent pass
    per pattern.
  * A table built from the very same executable is reused: entries whose bytes still
    match where they were recorded are kept, and only new or edited patterns are
    scanned. Adding a pattern therefore costs one scan, not a full rebuild.

One table serves every storefront a game ships on. The same build of a game sold on
Steam and on the Microsoft Store is two different executables -- the same code, laid out
at different addresses -- so the table holds a block of addresses per executable, and the
client seeds from the block whose identity matches the image it runs in. The patterns
themselves are one set: with --require-unique, a pattern that is not unique in every
executable fails the build, so a signature that only one storefront's build matches never
reaches a player.

Usage:
    python scripts/build_pattern_table.py <game.exe> <patterns.cpp> -o <project>/data/game.patterns

The executables are named by --exe (repeatable), by the environment variable named with
--exe-env, discovered from a Steam install with --steam-app/--steam-relative, and from a
Microsoft Store / Xbox app install with --msstore-family/--msstore-relative. A block for an
executable this machine does not have is carried over from the existing table.
"""

import argparse
import os
import re
import struct
import sys
import zlib
from collections import namedtuple
from multiprocessing import Pool
from pathlib import Path

MAGIC = b"FWPATTBL"
FORMAT_VERSION = 3
# The literal may sit inside one macro call, as KCDC's get_pattern(KCDC_HIDE("...")) does to
# keep it encrypted in the shipped binary; the bytes hashed at runtime are still the literal's.
# It may also be a run of adjacent literals, which read_literals() joins as the compiler does.
LITERAL_RE = re.compile(r'(?:get_pattern|get_opcode_address)\s*(?:<[^>]*>)?\s*\(\s*(?:[A-Za-z_]\w*\s*\(\s*)?((?:"[0-9A-Fa-f? ]*"\s*)+)')

# A C string literal, and a run of adjacent literals the compiler concatenates.
CXX_STRING = r'"(?:[^"\\]|\\.)*"'
LITERAL_RUN_RE = re.compile(r"(?:" + CXX_STRING + r"\s*)+")
CXX_STRING_RE = re.compile(CXX_STRING)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")

FNV1_PRIME = 1099511628211
FNV1_BASIS = 14695981039346656037
MASK64 = (1 << 64) - 1

# v3: magic, version, imageCount, patternSetHash -- at +16 in every version, which is what
# check_pattern_table.py reads -- and reserved space, then imageCount blocks. Each block is
# the identity the client matches the running image against (preferred base, SizeOfImage,
# file size), its entry count, a CRC32 of its entries, a CRC32 of the source executable so a
# rebuild can tell the block was built from the very same file, and the entries.
HEADER_FMT = "<8sIIQ24s"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
BLOCK_FMT = "<QIIIIII"
BLOCK_SIZE = struct.calcsize(BLOCK_FMT)
ENTRY_FMT = "<QII"
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)

assert HEADER_SIZE == 48 and BLOCK_SIZE == 32 and ENTRY_SIZE == 16

# One image's addresses: identity is (image_base, size_of_image, file_size), entries a list
# of (hash, rva) pairs.
Block = namedtuple("Block", "image_base size_of_image file_size file_crc entries")

# One pattern, reduced to what a scan needs: the longest wildcard-free run to search for,
# where that run sits inside the pattern, the concrete bytes outside it to verify, and
# the pattern's total length.
Prepared = namedtuple("Prepared", "needle offset checks length")


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
        found = ["".join(part[1:-1] for part in CXX_STRING_RE.findall(run)).strip() for run in LITERAL_RE.findall(text)]
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
    return bytes(image), image_base, size_of_image, len(raw), zlib.crc32(raw) & 0xFFFFFFFF


def prepare(literal: str) -> Prepared:
    """Reduce a literal to its longest wildcard-free run plus the bytes left to verify."""
    tokens = literal.split()
    best_len = best_off = run = 0
    for i, tok in enumerate(tokens):
        if tok == "?":
            run = 0
            continue
        run += 1
        if run > best_len:
            best_len, best_off = run, i - run + 1
    if best_len == 0:
        raise SystemExit("pattern has no concrete byte to anchor on: %s" % literal[:70])
    needle = bytes(int(tok, 16) for tok in tokens[best_off:best_off + best_len])
    checks = tuple((i, int(tok, 16)) for i, tok in enumerate(tokens)
                   if tok != "?" and not (best_off <= i < best_off + best_len))
    return Prepared(needle, best_off, checks, len(tokens))


def scan(image: bytes, prep: Prepared):
    """Every address the pattern matches, ascending.

    Restarting the search at hit+1 reports OVERLAPPING matches, which is what
    pattern::EnsureMatches does when it advances one byte past a hit: a self-overlapping
    pattern matches more often at runtime than a non-overlapping search would report, and
    a seeded pattern has to resolve to the same match set a scan would.
    """
    needle, offset, checks, length = prep
    find = image.find
    limit = len(image) - length
    out, pos = [], 0
    while True:
        hit = find(needle, pos)
        if hit < 0:
            return out
        pos = hit + 1
        start = hit - offset
        if start < 0 or start > limit:
            continue
        for i, byte in checks:
            if image[start + i] != byte:
                break
        else:
            out.append(start)


def matches_at(image: bytes, prep: Prepared, start: int) -> bool:
    """Whether the pattern still matches where a previous run recorded it."""
    needle, offset, checks, length = prep
    if start < 0 or start + length > len(image):
        return False
    if image[start + offset:start + offset + len(needle)] != needle:
        return False
    return all(image[start + i] == byte for i, byte in checks)


_POOL_IMAGE = None


def _pool_init(exe: str):
    # Each worker lays the image out for itself: the file is in the page cache by now, and
    # handing a 400 MB bytes object to every worker through a pickle costs more than the parse.
    global _POOL_IMAGE
    _POOL_IMAGE = map_image(Path(exe))[0]


def _pool_scan(prep: Prepared):
    return scan(_POOL_IMAGE, prep)


def scan_all(image: bytes, exe: Path, preps, jobs: int):
    """Scan the given patterns, in a process pool when there are enough to pay for one."""
    if len(preps) < 16 or jobs <= 1:
        return [scan(image, prep) for prep in preps]
    with Pool(jobs, initializer=_pool_init, initargs=(str(exe),)) as pool:
        return pool.map(_pool_scan, preps, chunksize=2)


def read_blocks(path: Path):
    """The blocks of an existing v3 table, or [] when there is none or it cannot be trusted.

    A block is reused for an executable only when its identity and source CRC are that
    executable's, so "reusable" means the bytes a previous run scanned are the bytes in hand.
    Every reused entry is re-verified against the image anyway before it is kept.
    """
    try:
        raw = path.read_bytes()
    except OSError:
        return []
    if len(raw) < HEADER_SIZE or raw[:8] != MAGIC:
        return []
    _, version, count, _, _ = struct.unpack_from(HEADER_FMT, raw, 0)
    if version != FORMAT_VERSION:
        return []
    blocks, off = [], HEADER_SIZE
    for _ in range(count):
        if off + BLOCK_SIZE > len(raw):
            return []
        base, size, fsize, n, entries_crc, file_crc, _ = struct.unpack_from(BLOCK_FMT, raw, off)
        off += BLOCK_SIZE
        blob = raw[off:off + n * ENTRY_SIZE]
        off += n * ENTRY_SIZE
        if len(blob) != n * ENTRY_SIZE or zlib.crc32(blob) & 0xFFFFFFFF != entries_crc:
            return []
        entries = [struct.unpack_from(ENTRY_FMT, blob, i * ENTRY_SIZE)[:2] for i in range(n)]
        blocks.append(Block(base, size, fsize, file_crc, entries))
    return blocks if off == len(raw) else []


def image_identity(exe: Path):
    """(image base, SizeOfImage, file size, file CRC32) of an executable, without mapping it."""
    raw = exe.read_bytes()
    e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
    opt = e_lfanew + 24
    magic = struct.unpack_from("<H", raw, opt)[0]
    base = struct.unpack_from("<Q", raw, opt + 24)[0] if magic == 0x20B else struct.unpack_from("<I", raw, opt + 28)[0]
    return base, struct.unpack_from("<I", raw, opt + 56)[0], len(raw), zlib.crc32(raw) & 0xFFFFFFFF


def locate_steam_game(appid: str, relative: str):
    """Where Steam put an installed app, or None.

    Build machines without the game fall back to the committed table, so this never fails
    loudly -- it just declines to find anything.
    """
    roots = []
    try:
        import winreg

        for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                          (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
            try:
                with winreg.OpenKey(hive, key) as handle:
                    value = winreg.QueryValueEx(handle, "SteamPath" if hive == winreg.HKEY_CURRENT_USER else "InstallPath")[0]
                    roots.append(Path(value))
            except OSError:
                continue
    except ImportError:
        pass
    roots.append(Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam")

    libraries = []
    for root in roots:
        vdf = root / "steamapps" / "libraryfolders.vdf"
        if not vdf.is_file():
            continue
        libraries.append(root)
        for match in re.finditer(r'"path"\s*"([^"]+)"', vdf.read_text(encoding="utf-8", errors="replace")):
            libraries.append(Path(match.group(1).replace("\\\\", "\\")))

    for library in libraries:
        manifest = library / "steamapps" / ("appmanifest_%s.acf" % appid)
        if not manifest.is_file():
            continue
        found = re.search(r'"installdir"\s*"([^"]+)"', manifest.read_text(encoding="utf-8", errors="replace"))
        if not found:
            continue
        exe = library / "steamapps" / "common" / found.group(1) / relative
        if exe.is_file():
            return exe
    return None


def locate_msstore_game(family: str, relative: str):
    """Where the Microsoft Store / Xbox app installed a package for this user, or None.

    The package's own executable is licence-protected, but the libraries beside it -- the
    game module a pattern table is built from -- read like any other file.
    """
    if sys.platform != "win32":
        return None
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32")
    count, length = wintypes.UINT(0), wintypes.UINT(0)
    if kernel32.GetPackagesByPackageFamily(family, ctypes.byref(count), None, ctypes.byref(length), None) != 122 or not count.value:
        return None  # 122: ERROR_INSUFFICIENT_BUFFER, the answer when the family is installed
    names = (ctypes.c_wchar_p * count.value)()
    buffer = ctypes.create_unicode_buffer(length.value)
    if kernel32.GetPackagesByPackageFamily(family, ctypes.byref(count), names, ctypes.byref(length), buffer) != 0:
        return None
    for name in names[:count.value]:
        size = wintypes.UINT(0)
        kernel32.GetPackagePathByFullName(name, ctypes.byref(size), None)
        path = ctypes.create_unicode_buffer(size.value)
        if kernel32.GetPackagePathByFullName(name, ctypes.byref(size), path) == 0:
            exe = Path(path.value) / relative
            if exe.is_file():
                return exe
    return None


def resolve_exes(exes=(), exe_env=None, steam_app=None, steam_relative=None, msstore_family=None, msstore_relative=None):
    """Every executable this machine has to build the table from: the explicit paths, the
    environment, a Steam install and a Microsoft Store install, de-duplicated."""
    found = [Path(exe) for exe in exes or () if exe]
    if exe_env:
        value = os.environ.get(exe_env)
        if value and Path(value).is_file():
            found.append(Path(value))
    if steam_app and steam_relative:
        found.append(locate_steam_game(steam_app, steam_relative))
    if msstore_family and msstore_relative:
        found.append(locate_msstore_game(msstore_family, msstore_relative))

    out, seen = [], set()
    for exe in found:
        if exe is not None and exe.is_file() and exe.resolve() not in seen:
            seen.add(exe.resolve())
            out.append(exe)
    return out


def default_jobs():
    return max(1, min(os.cpu_count() or 1, 8))


def build_block(exe: Path, literals, preps, known, jobs: int, require_unique: bool):
    """One executable's block, or None when a pattern does not resolve in it."""
    image, image_base, size_of_image, file_size, file_crc = map_image(exe)
    print("%s: %d bytes mapped to 0x%X..0x%X" % (exe, file_size, image_base, image_base + size_of_image))

    reusable = {}
    for block in known:
        if (block.image_base, block.size_of_image, block.file_size, block.file_crc) == (image_base, size_of_image, file_size, file_crc) and file_crc:
            for h, rva in block.entries:
                reusable.setdefault(h, []).append(rva)

    hits, pending = {}, []
    for lit in literals:
        recorded = reusable.get(framework_hash(lit, False))
        if recorded is not None and all(matches_at(image, preps[lit], rva) for rva in recorded):
            hits[lit] = recorded
        else:
            pending.append(lit)
    if reusable:
        print("  %d patterns reused from the existing table, %d to scan" % (len(hits), len(pending)))

    for lit, found in zip(pending, scan_all(image, exe, [preps[lit] for lit in pending], jobs or default_jobs())):
        hits[lit] = found

    entries, missing, ambiguous = [], [], []
    for lit in literals:
        found = hits[lit]
        if not found:
            missing.append(lit)
            continue
        if len(found) > 1:
            ambiguous.append((lit, len(found)))
        # Emit EVERY match, not just the first. The hint map is a multimap and the client
        # replays all of a hash's hints, so seeding the full match set is what makes a seeded
        # lookup agree with a scan -- callers that reject an ambiguous pattern still see the
        # ambiguity, and HogwartsMP's AobNearest still gets both twins to choose between.
        for with_nul in (False, True):
            entries.extend((framework_hash(lit, with_nul), rva) for rva in found)

    for lit, n in ambiguous:
        print("  ambiguous (%d matches%s): %s" % (n, "" if require_unique else ", all recorded", lit[:70]), file=sys.stderr)
    for lit in missing:
        print("  no match: %s" % lit[:70], file=sys.stderr)
    if missing or (require_unique and ambiguous):
        print("  %s: %d unmatched and %d ambiguous pattern(s)" % (exe.name, len(missing), len(ambiguous)), file=sys.stderr)
        return None

    # Ascending rva within a hash: std::multimap preserves insertion order for equal keys,
    # and a scan yields matches in ascending address order, so this keeps get(0) identical.
    entries.sort()
    return Block(image_base, size_of_image, file_size, file_crc, entries)


def build_table(exes, patterns: Path, out: Path, style: str = "call", jobs: int = 0, force: bool = False, require_unique: bool = False) -> int:
    literals = read_literals(patterns, style)
    if not literals:
        print("no patterns found in %s" % patterns, file=sys.stderr)
        return 1
    print("%d unique patterns" % len(literals))

    by_hash = {}
    for lit in literals:
        for with_nul in (False, True):
            h = framework_hash(lit, with_nul)
            if by_hash.setdefault(h, lit) != lit:
                print("hash collision: %s and %s" % (lit[:40], by_hash[h][:40]), file=sys.stderr)
                return 1

    preps = {lit: prepare(lit) for lit in literals}
    known = read_blocks(out)

    blocks, failed = [], 0
    for exe in exes:
        block = build_block(exe, literals, preps, [] if force else known, jobs, require_unique)
        if block is None:
            failed += 1
        else:
            blocks.append(block)
    if failed:
        print("refusing to write a table: the patterns do not resolve in %d executable(s)" % failed, file=sys.stderr)
        return 1

    # A storefront's executable this machine does not have keeps its block from the existing
    # table. Its patterns were not checked against the current set, so any it lacks are left
    # to the client's scan.
    built = {(b.image_base, b.size_of_image, b.file_size) for b in blocks}
    for block in known:
        if (block.image_base, block.size_of_image, block.file_size) not in built:
            print("kept the block for an executable not on this machine (file size %d): its patterns were not re-checked"
                  % block.file_size, file=sys.stderr)
            blocks.append(block)

    payload = b""
    for block in blocks:
        blob = b"".join(struct.pack(ENTRY_FMT, h, rva, 0) for h, rva in block.entries)
        payload += struct.pack(BLOCK_FMT, block.image_base, block.size_of_image, block.file_size,
                               len(block.entries), zlib.crc32(blob) & 0xFFFFFFFF, block.file_crc, 0) + blob
    header = struct.pack(HEADER_FMT, MAGIC, FORMAT_VERSION, len(blocks), pattern_set_hash(literals), b"")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + payload)
    print("wrote %s: %d patterns for %d executable(s), %d bytes" % (out, len(literals), len(blocks), len(header) + len(payload)))
    return 0


def add_exe_arguments(ap: argparse.ArgumentParser):
    """The ways of naming the game executable, shared with check_pattern_table.py."""
    ap.add_argument("--exe", type=Path, action="append", default=[], help="a game executable to resolve against (repeatable)")
    ap.add_argument("--exe-env", help="environment variable holding the path to a game executable")
    ap.add_argument("--steam-app", help="Steam app id to discover the game install from")
    ap.add_argument("--steam-relative", help="path to the executable inside the Steam app directory")
    ap.add_argument("--msstore-family", help="Microsoft Store package family to discover the game install from")
    ap.add_argument("--msstore-relative", help="path to the executable inside the Microsoft Store package")
    ap.add_argument("--require-unique", action="store_true",
                    help="fail when a pattern matches more than once in any executable")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe_positional", nargs="?", type=Path, metavar="exe",
                    help="the game executable to resolve against")
    ap.add_argument("patterns", type=Path, help="the source file holding the pattern literals")
    ap.add_argument("-o", "--out", type=Path, required=True, help="table to write")
    ap.add_argument("--style", choices=("call", "table"), default="call",
                    help="how the literals appear in the source: inside a get_pattern() call "
                         "(default) or in an Aob aggregate table")
    ap.add_argument("--jobs", type=int, default=0, help="parallel scans (default: up to 8)")
    ap.add_argument("--force", action="store_true", help="rescan every pattern instead of reusing the existing table")
    add_exe_arguments(ap)
    args = ap.parse_args()

    exes = resolve_exes([args.exe_positional] + args.exe, args.exe_env, args.steam_app, args.steam_relative,
                        args.msstore_family, args.msstore_relative)
    if not exes:
        print("no game executable: pass one, or set %s, or install the game"
              % (args.exe_env or "--exe"), file=sys.stderr)
        return 1
    return build_table(exes, args.patterns, args.out, args.style, args.jobs, args.force, args.require_unique)


if __name__ == "__main__":
    sys.exit(main())
