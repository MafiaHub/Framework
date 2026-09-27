"""Seed vcpkg's version cache with the exact port trees from the pinned image.

The pinned Windows vcpkg converts git checkout paths to forward slashes before
adding the extended-path prefix. Wine rejects those paths. Preparing this cache
on Linux avoids that operation without changing port versions or port contents.
"""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


source = Path('/opt/vcpkg')
destination = Path('/workspace/_external/msvc-wine/vcpkg')
manifest = json.loads(Path('/workspace/vcpkg.json').read_text())


def git(*args):
    return subprocess.check_output(
        ['git', '-c', f'safe.directory={source}', '-C', str(source), *args],
        text=True).strip()


if manifest['builtin-baseline'] != git('rev-parse', 'HEAD'):
    raise SystemExit('Rebuild the Windows image with VCPKG_REVISION matching vcpkg.json.')

for row in git('ls-tree', 'HEAD:ports').splitlines():
    metadata, name = row.split('\t')
    _, kind, digest = metadata.split()
    if kind != 'tree':
        continue
    parent = destination / 'buildtrees/versioning_/versions' / name
    cached = parent / digest
    if cached.is_dir():
        continue
    parent.mkdir(parents=True, exist_ok=True)
    # Atomic publication: an interrupted copy must not look like a valid cache.
    with tempfile.TemporaryDirectory(prefix='.prepare-', dir=parent) as temporary:
        staged = Path(temporary) / 'port'
        shutil.copytree(source / 'ports' / name, staged)
        staged.rename(cached)
