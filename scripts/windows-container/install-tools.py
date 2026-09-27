"""Install Windows tools, using the hashes from the pinned vcpkg checkout."""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import zipfile


def download(url, destination, algorithm, digest):
    print(f"Downloading {url}", flush=True)
    urllib.request.urlretrieve(url, destination)
    actual = hashlib.new(algorithm, destination.read_bytes()).hexdigest()
    if actual.lower() != digest.lower():
        raise RuntimeError(f"Checksum mismatch: {url}")


root = Path('/opt/tools')
root.mkdir()
tools = json.loads(Path('/opt/vcpkg/scripts/vcpkg-tools.json').read_text())['tools']
paths = []
for name in ('cmake', 'ninja', 'git', 'powershell-core', 'python3'):
    tool = next(t for t in tools if t['name'] == name and t.get('os') == 'windows'
                and t.get('arch') in ('amd64', 'x64'))
    folder = root / name
    folder.mkdir()
    archive = root / tool['archive']
    download(tool['url'], archive, 'sha512', tool['sha512'])
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as z:
            z.extractall(folder)
    else:
        subprocess.run(['7z', 'x', '-y', f'-o{folder}', str(archive)], check=True)
    archive.unlink()
    executable = folder / tool['executable']
    if not executable.is_file():
        raise RuntimeError(f"Missing tool: {executable}")
    paths.append('C:\\tools\\' + str(executable.parent.relative_to(root)).replace('/', '\\'))

# The .bat bootstrap requires Windows PowerShell 5. Download the same signed
# executable directly instead; obtain its SHA-256 from the official release.
metadata = dict(line.split('=', 1) for line in
                Path('/opt/vcpkg/scripts/vcpkg-tool-metadata.txt').read_text().splitlines())
tag = metadata['VCPKG_TOOL_RELEASE_TAG']
request = urllib.request.Request(
    f'https://api.github.com/repos/microsoft/vcpkg-tool/releases/tags/{tag}',
    headers={'User-Agent': 'MafiaHub-Windows-builder'})
with urllib.request.urlopen(request) as response:
    release = json.load(response)
asset = next(a for a in release['assets'] if a['name'] == 'vcpkg.exe')
algorithm, digest = asset['digest'].split(':', 1)
download(asset['browser_download_url'], Path('/opt/vcpkg/vcpkg.exe'), algorithm, digest)
root.joinpath('path.cmd').write_text('@set "PATH=' + ';'.join(paths) + ';%PATH%"\r\n')
