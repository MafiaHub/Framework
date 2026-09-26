#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
role=${1:?Usage: run-proton.sh client|server [arguments...]}
shift
case "$role" in
    client) executable=KCDCLauncher.exe ;;
    server) executable=KCDCServer.exe ;;
    *) echo "Expected client or server" >&2; exit 2 ;;
esac
steam_root=${STEAM_ROOT:-$HOME/.local/share/Steam}
proton=${PROTON_PATH:-$steam_root/steamapps/common/Proton - Experimental}
runtime=${STEAM_RUNTIME_PATH:-$steam_root/steamapps/common/SteamLinuxRuntime_4}
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$steam_root"
export STEAM_COMPAT_DATA_PATH="$root/builds/proton/$role"
export STEAM_COMPAT_INSTALL_PATH="$root/builds/build-64/bin"
export STEAM_COMPAT_APP_ID=1771300 SteamAppId=1771300 SteamGameId=1771300
export PROTON_LOG=1 PROTON_LOG_DIR="$root/builds/proton/logs/$role"
export PRESSURE_VESSEL_VARIABLE_DIR="$root/builds/proton/runtime-$role"
mkdir -p "$STEAM_COMPAT_DATA_PATH" "$PROTON_LOG_DIR" "$PRESSURE_VESSEL_VARIABLE_DIR"
cd "$STEAM_COMPAT_INSTALL_PATH"
exec "$runtime/_v2-entry-point" --verb=waitforexitandrun -- "$proton/proton" waitforexitandrun "$PWD/$executable" "$@"
