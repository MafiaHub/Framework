#!/usr/bin/env bash
set -euo pipefail
if [[ -z ${DISPLAY:-} ]]; then
    # xvfb-run must not be PID 1: Xvfb won't send its readiness signal to init.
    xvfb-run -a bash "$0" "$@"
    exit $?
fi
export WINEDLLOVERRIDES="mshtml="
export WINEPREFIX=/workspace/_external/msvc-wine/prefix
mkdir -p "$WINEPREFIX" /workspace/_external/msvc-wine/{downloads,binary-cache}
exec 9>/workspace/_external/msvc-wine/build.lock
flock -n 9 || { echo 'Another MSVC/Wine build is using this checkout.' >&2; exit 1; }
# vcpkg writes buildtrees and packages alongside itself. Persist that writable
# checkout on the mount, rather than writing into a root-owned image layer.
vcpkg_dir=/workspace/_external/msvc-wine/vcpkg
if [[ ! -e "$vcpkg_dir/.git" ]]; then
    cp -a /opt/vcpkg "$vcpkg_dir"
fi
if [[ $(git -C "$vcpkg_dir" rev-parse HEAD) != $(git -c safe.directory=/opt/vcpkg -C /opt/vcpkg rev-parse HEAD) ]]; then
    echo 'The cached vcpkg revision differs from the image. Use a matching image or a fresh checkout.' >&2
    exit 1
fi
python3 /workspace/scripts/windows-container/prepare-port-cache.py
wine_bin=$(command -v wine || command -v wine64 || echo /usr/lib/wine/wine64)
wine_server=$(command -v wineserver || echo /usr/lib/wine/wineserver)
wine_version=$("$wine_bin" --version)
echo "Windows builder: $wine_version"
if [[ ! -f "$WINEPREFIX/.framework-wine-version" ]] || [[ $(cat "$WINEPREFIX/.framework-wine-version") != "$wine_version" ]]; then
    WINEDLLOVERRIDES="mscoree,mshtml=" "$wine_bin" wineboot --update
    "$wine_server" -w
    printf '%s\n' "$wine_version" > "$WINEPREFIX/.framework-wine-version"
fi
ln -sfn /workspace "$WINEPREFIX/dosdevices/w:"
ln -sfn /opt/msvc "$WINEPREFIX/drive_c/msvc"
ln -sfn /opt/tools "$WINEPREFIX/drive_c/tools"
# Keep background PDB/compiler services alive for the duration of this build.
"$wine_server" -p
trap '"$wine_server" -k; "$wine_server" -w' EXIT
"$wine_bin" reg import 'W:\scripts\windows-container\sdk.reg' >/dev/null
log=/workspace/_external/msvc-wine/console.log
result=/workspace/_external/msvc-wine/exit-code.txt
: > "$log"
: > "$result"
"$wine_bin" wineconsole cmd /d /c 'W:\scripts\windows-container\run.cmd' "$@" &
build_pid=$!
tail --pid="$build_pid" --sleep-interval=0.1 -n +1 -f "$log" &
tail_pid=$!
status=0
wait "$build_pid" || status=$?
wait "$tail_pid" || true
if (( status != 0 )); then
    exit "$status"
fi
# Wine's console launcher can return zero even when its child failed. Read the
# batch process's result, and never accept a missing/stale result as success.
status=$(tr -d '\r\n ' < "$result")
if [[ ! "$status" =~ ^[0-9]+$ ]] || (( status > 255 )); then
    echo 'The Windows build did not return a valid exit code.' >&2
    exit 1
fi
exit "$status"
