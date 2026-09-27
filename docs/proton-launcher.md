# Starting a Windows client on Linux

Keep Linux Steam running and signed in, finish installing your game, and build
the launcher with the [MSVC container workflow](windows-container.md). Install
GE-Proton and its required Steam Linux Runtime through your usual Steam setup.
No changes to the game's Steam Launch Options or a separate Steam shortcut are
needed.

On the first launch, supply your game's Steam App ID (replace the sample number):

```bash
scripts/run_windows_client.sh --app-id 480
```

The script remembers that ID in this checkout. Subsequent launches are simply:

```bash
scripts/run_windows_client.sh
```

It automatically selects the single `*Launcher.exe` in `builds/build-64/bin`,
the newest installed GE-Proton, and the Steam runtime required by that Proton
version's manifest. It runs the launcher from its output directory so its client
DLL and other runtime files are found. Keep those files together as built.

Wayland and HDR default to `PROTON_ENABLE_WAYLAND=1` and `PROTON_ENABLE_HDR=1`.
Explicit environment values, including `0`, override those defaults. HDR still
requires support from the game, display and compositor.

## Optional overrides

- Select another launcher with `--exe builds/build-prod-64/bin/SampleLauncher.exe`.
- Select a GE-Proton installation with `PROTONPATH=/path/to/GE-Proton`.
- Select a nonstandard native Steam installation with `FW_STEAM_ROOT=/path/to/Steam`.
- Append launcher arguments after `--`.
- Use `--dry-run` to inspect the selected paths and command without starting
  anything or writing configuration.

The App ID is remembered per launcher name under `_external/proton/`. The script
also keeps a separate Proton prefix, shader cache and runtime working directory
there. It does not reuse the original game's Steam prefix, so existing saves and
prefix customizations are not automatically shared. The script does not edit
Steam settings or copy mod files into the game installation. Once launched, the
game and launcher can write their normal runtime data.

## Steam integration and validation

The framework calls `SteamAPI_Init()` and checks the logged-in user and game
installation. It does not search Linux for a Windows `steam.exe` process. The
script supplies the game's Steam identity and native Steam installation path
while invoking GE-Proton inside the matching
[Steam Linux Runtime](https://github.com/ValveSoftware/steam-runtime).
This is a development launcher using Steam's runtime entry point; that interface
can change between Steam releases.

The installed runtime and Proton selection and the launch command have been
checked without starting the game. Actual Steam API initialization, in-memory
game loading, hooks and rendering still require a game-runtime test. A successful
Windows build does not establish those behaviors under Proton.
