#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=${FW_WINDOWS_IMAGE:-localhost/mafiahub-msvc-wine:vs2022}
store="$repo/_external/msvc-wine"

if [[ ${1:-} == --help ]]; then
    echo 'Usage: scripts/run_windows_server.sh [--exe path/to/Server.exe] [server arguments]'
    echo 'With no --exe, runs the single *Server.exe in builds/build-64/bin.'
    exit 0
fi
if [[ ${1:-} == --exe ]]; then
    [[ $# -ge 2 ]] || { echo '--exe requires a path.' >&2; exit 2; }
    executable=$(realpath -e -- "$2")
    shift 2
else
    shopt -s nullglob
    servers=("$repo"/builds/build-64/bin/*Server.exe)
    if (( ${#servers[@]} != 1 )); then
        echo 'Expected one built server in builds/build-64/bin. Build it first, or select it with --exe.' >&2
        exit 2
    fi
    executable=${servers[0]}
fi
[[ -f "$executable" && "$executable" == "$repo/"* ]] || {
    echo 'The server executable must be a file inside this checkout.' >&2
    exit 2
}
relative=${executable#"$repo/"}

# Reuse the isolated store made by the build wrapper without requiring the
# caller to repeat its engine and storage options on every launch.
engine=${FW_CONTAINER_ENGINE:-}
if [[ -z "$engine" ]]; then
    if [[ -d "$store/storage/vfs" ]] && command -v podman >/dev/null; then
        engine=podman
    else
        engine=docker
    fi
fi
runtime=("$engine")
identity=(--user "$(id -u):$(id -g)")
if [[ "$engine" == podman ]]; then
    mkdir -p "$store"/{storage,run,tmp}
    chmod 700 "$store/run"
    runtime+=(--root "$store/storage" --runroot "$store/run" --tmpdir "$store/tmp" --storage-driver vfs)
    identity=(--userns=keep-id)
fi
if ! "${runtime[@]}" image inspect "$image" >/dev/null 2>&1; then
    echo "Build the Windows image first: FW_CONTAINER_ENGINE=$engine scripts/build_windows_container.sh image" >&2
    exit 1
fi
terminal=(-i)
if [[ -t 0 && -t 1 ]]; then terminal+=(-t); fi
exec "${runtime[@]}" run --rm --init "${terminal[@]}" "${identity[@]}" --network host \
    --mount "type=bind,src=$repo,dst=/workspace" \
    --workdir "/workspace/$(dirname -- "$relative")" \
    --entrypoint /bin/bash "$image" \
    /workspace/scripts/windows-container/server-entrypoint.sh "./$(basename -- "$relative")" "$@"
