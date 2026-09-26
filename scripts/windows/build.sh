#!/usr/bin/env bash
set -euo pipefail

framework_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
target=${1:-KCDCLauncher}
arch=${2:-64}
if [[ ( "$arch" != 64 && "$arch" != 32 ) || ! "$target" =~ ^[A-Za-z][A-Za-z0-9_]*$ ]]; then
    echo 'Usage: bash builds/build.bat <target> <32|64>' >&2
    exit 2
fi
cd "$framework_root"

if [[ ${FW_WINDOWS_CONTAINER:-0} != 1 ]]; then
    mkdir -p builds
    # Both canonical Windows trees use vcpkg's source buildtrees. Serialize the
    # container runs so two configure steps cannot replace each other's files.
    exec 9>builds/.windows-build.lock
    flock 9
    docker build -t framework-msvc-wine -f scripts/windows/Dockerfile scripts/windows
    mkdir -p builds/.wine builds/.cache builds/.runtime
    chmod 700 builds/.runtime
    exec docker run --rm --init -t --user "$(id -u):$(id -g)" \
        --mount "type=bind,source=$framework_root,target=/workspace" \
        -e FW_WINDOWS_CONTAINER=1 -e WINEPREFIX=/workspace/builds/.wine \
        -e XDG_CACHE_HOME=/workspace/builds/.cache \
        -e XDG_RUNTIME_DIR=/workspace/builds/.runtime \
        -e npm_config_cache=/workspace/builds/.cache/npm \
        -e VCPKG_DEFAULT_BINARY_CACHE=/workspace/builds/.cache \
        -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-8}" \
        -e "VCPKG_MAX_CONCURRENCY=${CMAKE_BUILD_PARALLEL_LEVEL:-8}" \
        framework-msvc-wine bash builds/build.bat "$target" "$arch"
fi

if [[ "$target" == KCDC* ]]; then
    npm --prefix code/projects/mod/ui ci --no-audit --no-fund
    npm --prefix code/projects/mod/ui run build
    if [[ "$target" == KCDCServer ]]; then
        npm --prefix builds/node-tools install --no-save --package-lock=false --no-audit --no-fund pnpm@10.4.1
        builds/node-tools/node_modules/.bin/pnpm --dir code/projects/mod/resources/kcdc-gamemode install --frozen-lockfile --store-dir "$framework_root/builds/.cache/pnpm"
        builds/node-tools/node_modules/.bin/pnpm --dir code/projects/mod/resources/kcdc-gamemode run build
    fi
fi

if [[ "$target" == Mafia1OnlineClient ]]; then
    # The CEF front end is optional at runtime, so a failed UI build keeps the
    # previous resources/ui instead of failing the client.
    bash code/projects/mafia1online/tools/build_ui.sh || echo "Mafia1Online UI build failed; keeping the existing resources/ui" >&2
fi

wineserver -p
wineboot --update
trap 'wineserver -k; wineserver -w' EXIT

# vcpkg's Windows bootstrap requires PowerShell. Download the pinned tool
# with the container's native tools before entering the Windows build.
vcpkg_pin=$(python3 -c 'import json; manifest = json.load(open("vcpkg.json")); assert not manifest.get("overrides"), "Wine port overlay requires the unmodified baseline versions"; print(manifest["builtin-baseline"])')
if [[ ! -f vendors/vcpkg/scripts/buildsystems/vcpkg.cmake ]]; then
    git init vendors/vcpkg
    git -C vendors/vcpkg remote add origin https://github.com/microsoft/vcpkg.git
    git -C vendors/vcpkg fetch --depth 1 origin "$vcpkg_pin"
    git -C vendors/vcpkg checkout --detach FETCH_HEAD
fi
if [[ $(git -C vendors/vcpkg rev-parse HEAD) != "$vcpkg_pin" ]]; then
    echo "The Wine build requires vendors/vcpkg at the manifest baseline: $vcpkg_pin" >&2
    exit 1
fi
git -C vendors/vcpkg diff --exit-code HEAD -- ports versions
cp /opt/windows/vcpkg.exe vendors/vcpkg/vcpkg.exe

# Upstream's environment script is generated for the installed SDK version.
set +u
target_arch=x64
if [[ "$arch" == 32 ]]; then target_arch=x86; fi
source "/opt/msvc/bin/${target_arch}/msvcenv.sh"
set -u
export VCToolsInstallDir="$MSVCDIR\\"
export WINEPATH="Z:\\opt\\windows\\cmake\\bin;Z:\\opt\\windows\\ninja;Z:\\opt\\windows\\git\\cmd;Z:\\opt\\windows\\node;Z:\\opt\\windows\\python;$WINEPATH"
export VCPKG_DEFAULT_BINARY_CACHE='Z:\workspace\builds\.cache'
export npm_config_cache='Z:\workspace\builds\.cache\npm'
export FW_WINE_BUILD=1
wine cmd /d /c "builds\\build.bat $target $arch"
cp "/opt/msvc/VC/Redist/MSVC/"*/"${target_arch}"/Microsoft.VC143.CRT/*.dll "builds/build-${arch}/bin/"
