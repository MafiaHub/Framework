# PE Loader

This directory contains the framework's PE (Portable Executable) loader, which allows loading a game executable into the launcher's process space without creating a new process.

## How It Works

The PE loader performs the following steps:

1. **Map the executable** - Memory-map the game's EXE file
2. **Load sections** - Copy PE sections (.text, .data, .rdata, etc.) into the launcher's address space
3. **Apply relocations** - Fix absolute addresses since the game loads at a different base than intended
4. **Resolve imports** - Hook IAT (Import Address Table) to redirect API calls as needed
5. **Setup TLS** - Initialize Thread Local Storage for the game's CRT
6. **Setup exception handling** - Register the game's exception handlers (x64)
7. **Invoke entry point** - Jump to the game's original entry point

## TLS (Thread Local Storage) Handling

The loader supports two approaches for TLS setup, configurable via `ProjectConfiguration::useDirectTlsSlot0`:

### Approach 1: Direct Slot 0 (`useDirectTlsSlot0 = true`)

Used for games that expect their CRT to use TLS slot 0 (e.g., Cyberpunk 2077).

**Requirements:**
- The launcher EXE must declare a "sacrificial" TLS buffer that claims slot 0
- This buffer gets overwritten with the game's TLS template

**How it works:**
1. Launcher EXE declares `__declspec(thread)` variable (claims slot 0)
2. Launcher's CRT uses a different slot for its own TLS (locale, etc.)
3. PE loader copies game's TLS template directly to slot 0
4. Game code expecting slot 0 finds its TLS data intact

**Example launcher code:**
```cpp
// Must be in the launcher EXE, not a DLL
__declspec(thread) uint8_t g_sacrificialTlsBuffer[32768];

// Touch early to ensure allocation
struct TlsInit {
    TlsInit() { g_sacrificialTlsBuffer[0] = 1; }
} g_tlsInit;
```

### Approach 2: Allocated Slot (`useDirectTlsSlot0 = false`, default)

Used for games that don't require slot 0 (e.g., Mafia series).

**How it works:**
1. Framework's loader DLL (`FrameworkLoaderData.dll`) has its own TLS buffer
2. `GetThreadLocalStorage` export provides the allocated slot index
3. PE loader copies game's TLS template to this allocated slot
4. Game's TLS index variable is patched to point to the allocated slot

This is the traditional approach and works for most games.

## Configuration

```cpp
Framework::Launcher::ProjectConfiguration config;
config.useDirectTlsSlot0 = true;  // Use direct slot 0 (requires sacrificial buffer)
config.useDirectTlsSlot0 = false; // Use allocated slot (default)
```

When the launcher and mapped game both use the dynamic UCRT and startup aborts in
`_register_thread_local_exe_atexit_callback`, the launcher has already claimed the
process-wide EXE TLS-destructor slot. Suppress only the mapped game's duplicate registration:

```cpp
config.suppressThreadLocalExeAtexitCallback = true;
```

Leave this disabled unless the duplicate-registration failure is confirmed; a game whose
launcher did not claim the slot should retain the normal UCRT registration. Suppression means
the mapped game's primary-thread TLS destructors are not run during process exit; thread
attach/detach callbacks and normal execution are unaffected.

## When to Use Which Approach

| Game Behavior | Approach | Config |
|--------------|----------|--------|
| Game CRT expects TLS at slot 0 | Direct Slot 0 | `useDirectTlsSlot0 = true` |
| Game works with any TLS slot | Allocated Slot | `useDirectTlsSlot0 = false` |

If you encounter crashes related to TLS (often manifesting as NULL pointer access at small offsets like 0x14, 0x24), try switching approaches.

## Game Path Resolution

The launcher resolves the game directory from the configured `platform`:

- `STEAM` - the Steam client is asked for the app's install directory (`steamAppId`).
- `EPIC` - the Epic launcher's manifests are matched by `epicAppName`, else by `executableName`.
- `CLASSIC` - the stored `classicGamePath` is used, or the player is prompted for the game executable when `promptForGameExe` is set.
- `ROCKSTAR` - the Rockstar Games Launcher's registry entries are matched by `rockstarTitleKey`, else by the title holding `executableName`.

Store lookups fail for players who own the game outside that store (or simply do not have the client running). Set `allowManualGamePathFallback` to keep the store as the primary path and drop to the manual prompt instead of aborting:

```cpp
config.platform                    = Framework::Launcher::ProjectPlatform::STEAM;
config.steamAppId                  = 50130;
config.allowManualGamePathFallback = true;
config.promptTitle                 = "Select your game executable";
config.promptFilter                = "game.exe";
config.promptFilterName            = "game.exe";
```

The picked file must be named `executableName`, and when `useAlternativeWorkDir` is set the work dir is stripped back off so the resulting path is the game root - the same thing Steam and Epic hand back. A manual pick is remembered in the launcher's JSON config (`game_path` plus `game_path_manual`) and takes priority over the store on later runs, so the prompt only appears once.

## Rockstar Games Launcher Titles

A title installed through the Rockstar Games Launcher is wrapped. Two things follow from that.

**It does not start at its own entry point.** RGL prepends a stub in a section of its own
(`.rkstr`) that resolves `LoadLibraryW` out of the PEB, loads `MTLX.dll` - Rockstar's wrapper, whose
own PDB path calls it `RockstarWrapper` - spins on that module's `X` export until the launcher
answers, and only then jumps to the game's real entry point. `RGL::ResolveEntryStub` decodes that
final jump out of the mapped image, so the real entry point comes from the image rather than a
hardcoded offset.

**Its code sections are encrypted on disk, and only the wrapper decrypts them.** Mapping the image
and entering past the stub therefore runs into ciphertext (measured on San Andreas: `.text` pages
`0x401000`-`0x4FB000` are at 8.00 bits of entropy per byte, and executing them faults with
`STATUS_PRIVILEGED_INSTRUCTION`). The wrapper will not do that decryption for a mapped image
either: it only authorises a process the launcher itself started, and when it sees one it does not
recognise it hands the launch back to the launcher, which starts its own copy of the game while the
mapped one exits.

So the decrypted code has to come from a run the launcher did authorise. `useRockstarImageSnapshot`
does exactly that, following the executable snapshot FiveM uses for the same class of wrapper:

```cpp
config.platform                 = Framework::Launcher::ProjectPlatform::ROCKSTAR;
config.rockstarTitleKey         = L"GTA: San Andreas";
config.useRockstarImageSnapshot = true;
```

On the first run for a given build the launcher starts the game once - which the wrapper hands to
the Rockstar Games Launcher - waits until every ciphertext page in the authorised process has been
decrypted, reads the executable sections out of it, and caches them next to the launcher as
`cache/<name>_image_snapshot.bin`, keyed by the executable's CRC32. Every later run maps the image
from disk, lays the cached code back over it, and enters at the game's own entry point. A game
update changes the checksum and the capture happens again.

The completeness check matters: the wrapper decrypts progressively, and a capture taken while it is
still working bakes half-decrypted code into the cache. `ImageSnapshot::CaptureFrom` only accepts a
capture once every page that is ciphertext on disk has changed in memory.

## Launcher Image Layout

The game is mapped into the launcher's own image, at the launcher's image base: the game's
headers overwrite the launcher's, and its sections land from RVA `0x1000` on. The launcher is
laid out to leave that range empty:

| Section | Holds |
|---|---|
| `.fwgame` | The game reservation: empty, uninitialised, first after the headers |
| `.pdata`, `.fwend`, `.cld`, `.clr` | The launcher's own exception table and data (`.data` and `.rdata` merged) |
| `.zdata` | The launcher's own code (`.text` merged), kept last by `/LAST:.zdata` |
| `.rsrc` | Resources |

FrameworkLoader forces the rest: `/BASE:0x140000000` (`0x400000` on 32-bit) and
`/DYNAMICBASE:NO`, so the launcher sits where a 64-bit game expects to be.

### Sizing the game reservation

The reservation must cover the game executable's `SizeOfImage`, and nothing more is gained by
making it larger. Every launcher gets a default sized for the largest game any project maps -
`0x6fffffff` (1.75 GiB) on 64-bit and `0x2500000` on 32-bit. A small executable should set its
own, because an image that is almost entirely empty section with its entry point at the far end
reads as a custom packer to antivirus heuristics:

```cmake
# KingdomCome.exe maps to 1.5 MiB; the game itself is WHGame.dll, which Windows loads on its own
target_compile_definitions(KCDCLauncher PRIVATE FW_LAUNCHER_GAME_RESERVE=0x1000000)
```

Read the executable's `SizeOfImage` from its optional header and leave room for game updates to
grow it. Only the executable counts: modules the game loads itself, such as a CryEngine game DLL
(see [cryengine_games.md](../../../../docs/cryengine_games.md)), get their own address from Windows.

The reservation itself is compiled into each launcher by `game_reserve.cpp`, which FrameworkLoader
adds as an interface source, so the launcher's own definition reaches it. `project.cpp` bounds it
with markers in `.fwgame$a` and `.fwgame$c`; the linker orders `$`-suffixed parts of a section by
suffix, which is the only section ordering it documents. Where `.fwgame` lands among the other
sections is not documented, so `RunWithPELoading()` checks it on every launch and refuses, with a
message box and a logged `Game image ... does not fit the reservation` line, when the reservation
does not start at image base + `0x1000` or does not reach the game's `SizeOfImage`. A game update
that outgrows a tight reservation fails there rather than overwriting the launcher.

## Product Identity

A shipped binary should say what it is and who publishes it: Explorer, SmartScreen and antivirus
reputation systems key on that, and a launcher that only identifies as the generic
`MafiaHub.Framework.Launcher` gives them nothing stable to attach a reputation to.
`fw_set_binary_identity()` (in `cmake/FrameworkBinaryIdentity.cmake`, available to every project)
stamps a target with a `VERSIONINFO` resource, an optional icon and, for a launcher, the identity in
its application manifest:

```cmake
fw_set_binary_identity(KCDCLauncher PRODUCT "Kingdoms Connected" VERSION ${KCDC_VERSION} COMPANY "MafiaHub" DESCRIPTION "Kingdoms Connected Launcher" ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/kcdc.ico")
fw_set_binary_identity(KCDCClient PRODUCT "Kingdoms Connected" VERSION ${KCDC_VERSION} COMPANY "MafiaHub" DESCRIPTION "Kingdoms Connected Client")
```

- `VERSION` takes one to four numbers and is padded to the four the fixed version and the manifest
  need; `1.5.3` becomes `1.5.3.0`. `DESCRIPTION` defaults to `PRODUCT`, `COPYRIGHT` to
  `Copyright (C) <COMPANY>`. `OriginalFilename` comes from the target's real output name.
- The launcher's manifest is generated from `launcher.manifest.in` with only its identity changed -
  `<COMPANY>.<PRODUCT>.<target>` stripped to letters and digits, the padded version, the build's
  architecture and the description. Compatibility, DPI awareness, long paths, `asInvoker` and
  Common Controls stay exactly as in the generic one. FrameworkLoader embeds it through the
  `FW_LAUNCHER_MANIFEST` property of the consuming launcher, and a launcher that never calls the
  helper keeps the generic manifest.
- Only Windows binaries are stamped; the call does nothing elsewhere, so a server built for Linux
  can make it unconditionally.

`CompanyName` should match the subject of the certificate the release is signed with.

## Launch Types

`ProjectLaunchType::PE_LOADING` is the default and what every project uses. The cross-process
`DLL_INJECTION` type - `CreateProcess` suspended, then `VirtualAllocEx`, `WriteProcessMemory` and
`CreateRemoteThread` into `LoadLibraryW` - and `RGL::ProcessMonitor`, which injects the same way,
are only compiled with `-DFW_DLL_INJECTION=ON`. Those imports alone get a launcher flagged as a
trojan by Windows Defender, so without the option the enum value does not exist and a project
asking for it fails to compile.

## Files

- `project.cpp` / `project.h` - Main launcher project class and configuration
- `game_reserve.cpp` - the game reservation, compiled into each launcher and sized by `FW_LAUNCHER_GAME_RESERVE`
- `loaders/exe_ldr.cpp` / `exe_ldr.h` - PE executable loader implementation
- `data/tls.cpp` - TLS buffer for allocated slot approach (in FrameworkLoaderData.dll)
- `rgl_bypass.cpp` / `rgl_bypass.h` - Rockstar Games Launcher entry-stub decoding and signature-check bypasses; `ProcessMonitor` only with `FW_DLL_INJECTION`
- `loaders/image_snapshot.cpp` / `image_snapshot.h` - capture and replay of the code a store wrapper decrypts at runtime
