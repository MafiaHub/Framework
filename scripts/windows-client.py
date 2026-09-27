"""Launch a built Steam client with GE-Proton without editing Steam settings."""

import argparse
import os
from pathlib import Path
import re
import shlex
import sys


REPO = Path(__file__).resolve().parent.parent


def vdf_values(path, key):
    # Only leaf strings are needed from Steam's generated VDF manifests.
    if not path.is_file():
        return []
    pattern = r'"' + re.escape(key) + r'"\s+"((?:\\.|[^"\\])*)"'
    return [re.sub(r'\\([\\"])', r'\1', value)
            for value in re.findall(pattern, path.read_text())]


def install_directory(libraries, app_id):
    for library in libraries:
        manifest = library / "steamapps" / f"appmanifest_{app_id}.acf"
        directories = vdf_values(manifest, "installdir")
        if directories:
            return library / "steamapps/common" / directories[0]
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-id", help="Steam App ID; remembered per launcher in this checkout")
    parser.add_argument("--exe", type=Path, help="launcher executable (default: single built *Launcher.exe)")
    parser.add_argument("--dry-run", action="store_true", help="show command without launching or writing files")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="launcher arguments after --")
    options = parser.parse_args()

    if options.exe:
        launcher = options.exe.resolve(strict=True)
    else:
        launchers = list((REPO / "builds/build-64/bin").glob("*Launcher.exe"))
        if len(launchers) != 1:
            parser.error("Build a launcher first, or select one with --exe.")
        launcher = launchers[0].resolve(strict=True)
    if not launcher.is_file() or not launcher.is_relative_to(REPO):
        parser.error("The launcher must be a file inside this checkout.")

    state = REPO / "_external/proton" / launcher.stem
    identity_file = state / "app-id"
    app_id = options.app_id
    if app_id is None and identity_file.is_file():
        app_id = identity_file.read_text().strip()
    if not app_id or not re.fullmatch(r"[1-9][0-9]*", app_id):
        parser.error("Supply the game's Steam App ID once: --app-id <number>.")

    steam_candidates = [Path(os.environ["FW_STEAM_ROOT"])] if os.environ.get("FW_STEAM_ROOT") else [
        Path.home() / ".steam/steam",
        Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "Steam",
    ]
    steam = next((path.resolve() for path in steam_candidates if (path / "steamapps").is_dir()), None)
    if steam is None:
        parser.error("Native Linux Steam was not found. Set FW_STEAM_ROOT to its installation directory.")
    libraries = list(dict.fromkeys([steam] + [Path(path) for path in
        vdf_values(steam / "steamapps/libraryfolders.vdf", "path")]))

    if os.environ.get("PROTONPATH"):
        proton = Path(os.environ["PROTONPATH"]).expanduser().resolve()
    else:
        candidates = [path for path in (steam / "compatibilitytools.d").glob("GE-Proton*")
                      if (path / "proton").is_file()]
        if not candidates:
            parser.error("Install GE-Proton in Steam, or set PROTONPATH to an installed GE-Proton directory.")
        # Natural version ordering, so GE-Proton11 sorts after GE-Proton9.
        proton = max(candidates, key=lambda path: [int(part) if part.isdigit() else part
                                                   for part in re.split(r"(\d+)", path.name)])
    if not (proton / "proton").is_file():
        parser.error(f"No Proton executable in {proton}.")
    runtime_ids = vdf_values(proton / "toolmanifest.vdf", "require_tool_appid")
    if len(runtime_ids) != 1:
        parser.error("The selected Proton tool does not declare a Steam runtime dependency.")
    runtime = install_directory(libraries, runtime_ids[0])
    if runtime is None or not (runtime / "_v2-entry-point").is_file():
        parser.error(f"Install the required Steam runtime first: steam steam://install/{runtime_ids[0]}")
    game = install_directory(libraries, app_id)
    if game is None or not game.is_dir():
        parser.error("Finish installing the game in Linux Steam before launching.")

    env = os.environ.copy()
    # These may be inherited from another launcher; this is a Steam launch.
    for key in ("UMU_ID", "UMU_GAME_ID", "UMU_STEAM_GAME_ID"):
        env.pop(key, None)
    settings = {
        "SteamAppId": app_id,
        "SteamGameId": app_id,
        "STEAM_COMPAT_APP_ID": app_id,
        "STEAM_COMPAT_CLIENT_INSTALL_PATH": str(steam),
        "STEAM_COMPAT_INSTALL_PATH": str(game),
        "STEAM_COMPAT_DATA_PATH": str(state / "compatdata"),
        "STEAM_COMPAT_SHADER_PATH": str(state / "shadercache"),
        "STEAM_COMPAT_LIBRARY_PATHS": ":".join(map(str, libraries)),
        "STEAM_COMPAT_TOOL_PATHS": f"{proton}:{runtime}",
        "STEAM_COMPAT_MOUNTS": str(REPO),
        "PRESSURE_VESSEL_VARIABLE_DIR": str(state / "runtime"),
        "PROTON_LOG_DIR": str(state),
        "PROTON_CRASH_REPORT_DIR": str(state / "crashes"),
        "PROTON_ENABLE_WAYLAND": env.get("PROTON_ENABLE_WAYLAND", "1"),
        "PROTON_ENABLE_HDR": env.get("PROTON_ENABLE_HDR", "1"),
    }
    env.update(settings)
    arguments = options.args[1:] if options.args[:1] == ["--"] else options.args
    command = [str(runtime / "_v2-entry-point"), "--verb=waitforexitandrun", "--",
               str(proton / "proton"), "waitforexitandrun", str(launcher), *arguments]
    if options.dry_run:
        print(f"Working directory: {launcher.parent}")
        for key, value in settings.items():
            print(f"{key}={shlex.quote(value)}")
        print(shlex.join(command))
        return

    for directory in (state / "compatdata", state / "shadercache", state / "runtime", state / "crashes"):
        directory.mkdir(parents=True, exist_ok=True)
    identity_file.write_text(app_id + "\n")
    print(f"Launching {launcher.name} with {proton.name}. Keep Linux Steam running and signed in.", flush=True)
    os.chdir(launcher.parent)
    os.execve(command[0], command, env)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
