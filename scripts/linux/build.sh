#!/usr/bin/env bash
set -euo pipefail

framework_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
target=${1:-}
if [[ ! "$target" =~ ^[A-Za-z][A-Za-z0-9_]*$ || ${2:-} != linux64 ]]; then
    echo 'Usage: bash builds/build.bat <target> linux64' >&2
    exit 2
fi

cd "$framework_root"
if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
    docker build -t framework-msvc-wine -f scripts/windows/Dockerfile scripts/windows
    exec docker run --rm --init --user "$(id -u):$(id -g)" \
        --mount "type=bind,source=$framework_root,target=/workspace" \
        -e FW_LINUX_CONTAINER=1 -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-8}" \
        framework-msvc-wine bash builds/build.bat "$target" linux64
fi

cmake -S . -B builds/build-linux-64 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DVCPKG_TARGET_TRIPLET=x64-linux-mh -DVCPKG_HOST_TRIPLET=x64-linux-mh
cmake --build builds/build-linux-64 --target "$target"
