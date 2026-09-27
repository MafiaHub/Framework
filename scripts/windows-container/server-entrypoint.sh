#!/usr/bin/env bash
set -euo pipefail
if [[ -z ${DISPLAY:-} ]]; then
    xvfb-run -a bash "$0" "$@"
    exit $?
fi
export WINEPREFIX=/workspace/_external/msvc-wine/server-prefix
mkdir -p "$WINEPREFIX"
exec 9>/workspace/_external/msvc-wine/server.lock
flock -n 9 || { echo 'A Windows server is already running in this checkout.' >&2; exit 1; }
trap 'wineserver -k; wineserver -w' EXIT
if [[ ! -f "$WINEPREFIX/.server-initialized" ]]; then
    echo 'Preparing the Windows server runtime (first launch only)...'
    WINEDLLOVERRIDES="mscoree,mshtml=" wineboot --update
    wineserver -w
    touch "$WINEPREFIX/.server-initialized"
fi
ln -sfn /workspace "$WINEPREFIX/dosdevices/w:"
wine reg import 'W:\scripts\windows-container\sdk.reg' >/dev/null
wine "$@" <&0 &
server_pid=$!
trap 'kill -INT "$server_pid" 2>/dev/null || true' INT
trap 'kill -TERM "$server_pid" 2>/dev/null || true' TERM
status=0
wait "$server_pid" || status=$?
exit "$status"
