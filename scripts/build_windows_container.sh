#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
engine=${FW_CONTAINER_ENGINE:-docker}
image=${FW_WINDOWS_IMAGE:-localhost/mafiahub-msvc-wine:vs2022}
runtime=("$engine")
# Optional isolated Podman store; useful on hosts without Docker socket access.
if [[ "$engine" == podman ]]; then
    store="$repo/_external/msvc-wine"
    mkdir -p "$store"/{storage,run,tmp}
    chmod 700 "$store/run"
    runtime+=(--root "$store/storage" --runroot "$store/run" --tmpdir "$store/tmp" --storage-driver vfs)
fi
case "${1:-}" in
    image)
        exec "${runtime[@]}" build -t "$image" "$repo/scripts/windows-container"
        ;;
    ''|-h|--help)
        printf '%s\n' 'Usage: scripts/build_windows_container.sh image' \
            '       scripts/build_windows_container.sh <target> [Debug|Release|RelWithDebInfo] [32|64]' \
            'Set FW_CONTAINER_ENGINE=podman to use an isolated repository-local Podman store.'
        exit 0
        ;;
esac
target=$1
config=${2:-Debug}
arch=${3:-64}
jobs=${FW_BUILD_JOBS:-8}
[[ "$arch" == 32 || "$arch" == 64 ]] || { echo 'Invalid architecture (expected 32 or 64)' >&2; exit 2; }
[[ $# -le 3 ]] || { echo 'Too many arguments' >&2; exit 2; }
[[ "$target" =~ ^[A-Za-z0-9_.+-]+$ ]] || { echo 'Invalid target' >&2; exit 2; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'FW_BUILD_JOBS must be a positive integer' >&2; exit 2; }
case "$config" in Debug|Release|RelWithDebInfo) ;; *) echo 'Invalid configuration' >&2; exit 2;; esac
user_options=(--user "$(id -u):$(id -g)")
if [[ "$engine" == podman ]]; then
    user_options=(--userns=keep-id)
fi
exec "${runtime[@]}" run --rm --init "${user_options[@]}" \
    --env FW_CMAKE_ARGS \
    --env "CMAKE_BUILD_PARALLEL_LEVEL=$jobs" --env "VCPKG_MAX_CONCURRENCY=$jobs" \
    --mount "type=bind,src=$repo,dst=/workspace" \
    --entrypoint /bin/bash "$image" /workspace/scripts/windows-container/entrypoint.sh "$target" "$arch" "$config"
