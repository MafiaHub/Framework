# CryEngine game support

Most games the framework targets are monolithic: the executable *is* the game code.
Mafia: Definitive Edition, Mafia III, Cyberpunk 2077 and Hogwarts Legacy all ship a
single large binary, so `GetModuleHandle(nullptr)` is the game and everything is
resolvable as soon as the client DLL is initialised.

CryEngine titles are built differently. The executable is a small bootstrap whose only
job is to load the real game module:

| | file | size | `.text` |
|---|---|---|---|
| Kingdom Come: Deliverance II | `KingdomCome.exe` | 1.46 MB | 0.03 MB |
| | `WHGame.dll` | 85 MB | 58 MB |

Two framework assumptions break as a result, and both have to be handled to boot such
a game. If you are adding another CryEngine title, expect to need both.

## 1. The pattern scanner must be based on the game module

`hook::set_base()` defaults to `GetModuleHandle(nullptr)`, and `hook::pattern` scans
`[base, base + SizeOfImage)`. On a CryEngine game that searches the 34 KB bootstrap
instead of the game, so every pattern misses and `pattern::count(1)` trips its
assertion — a native "pattern mismatch on count" dialog with no indication of which
pattern or why.

Timing matters just as much. `InitClient` runs from the launcher's
`GetStartupInfoW`/`GetCommandLineW` stubs during the bootstrap's CRT startup, long
before the game module is loaded. Even with the right base, there is nothing to scan
yet.

Use `Framework::Utils::GameModule` instead of calling `hook::set_base()` directly:

```cpp
#include <utils/game_module.h>

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    MH_Initialize();

    Framework::Utils::GameModule::WhenReady(L"WHGame.dll", [](HMODULE) {
        SDK::Patterns::InitPatterns();

        InitFunction::RunAll();
        MH_EnableHook(MH_ALL_HOOKS);
    });
}
```

`WhenReady` waits for the loader to report the module, points the pattern scanner at
it, and only then invokes the callback. Resolve patterns and install hooks from the
callback, never before it. Pass an empty module name for a monolithic game and the
callback runs inline on the executable, which is what the other projects do today.

Detection uses `LdrRegisterDllNotification` and matches the loader's resolved base
name, so it does not care which `LoadLibrary` variant the bootstrap calls or what
string it passes. It gives up after `GameModule::kWaitTimeoutMs` and logs an error
rather than hanging.

## 2. The process must report the game executable, not the launcher

`PE_LOADING` maps the game image into the launcher process, but the loader data still
describes the launcher. The launcher compensates by redirecting `GetModuleFileName*`
and friends through `SetFunctionResolver`, which rewrites the **mapped executable's
import table**.

That covers all game code when the game is the executable. It covers 34 KB out of
85 MB on a CryEngine title: the game module is loaded by the Windows loader and binds
the genuine `kernel32` exports, so it never sees the redirect. CryEngine derives its
engine root from

```c
GetModuleFileNameW(GetModuleHandleA(nullptr), path, 512);
```

and walks up looking for an `Engine` folder. It finds the launcher's directory,
gives up, and calls `CSystem::FatalError("Unable to locate CryEngine root folder…")`.

Worth knowing when debugging this: that fatal-error handler then calls
`DumpMemStats`, which reads the `sys_dll_game` CVar. The CVar is not registered yet,
so the process dies with an access violation inside the error reporter and the real
message is never shown. The crash address is nowhere near the actual fault.

`Framework::Launcher::Loaders::ApplyMappedImageIdentity` fixes this at the source. It
rewrites the main image's `LDR_DATA_TABLE_ENTRY` (`FullDllName`, `BaseDllName`) and
`ProcessParameters->ImagePathName`, so every module in the process resolves the game
path no matter how it was loaded or which path API it calls. It runs automatically for
every `PE_LOADING` launch — projects do not opt in.

### Limits

* **Command line is not covered.** `kernel32` caches it at process init, so
  `GetCommandLineW/A` still returns the launcher's arguments for modules outside the
  mapped image. Code reaching it through the CRT is fine —
  `SynchronizeUCRTCommandLine()` patches ucrt's `__p__acmdln`/`__p__wcmdln`, which is
  the path CryEngine takes. `additionalLaunchArguments` would not reach a game module
  that calls the raw API.
* **`GetModuleHandleW(L"Game.exe")` still returns null.** Renaming `BaseDllName` does
  not rehash the loader's module table. The launcher's `GetModuleHandle*` import
  redirects cover the mapped executable; nothing covers other modules.
* Paths that do not fit a loader string's 16-bit byte length are rejected, leaving the
  identity unchanged rather than truncating it.

## Checklist for a new CryEngine game

1. Point `config.executableName` at the bootstrap executable, `PE_LOADING` as usual.
2. Set `config.alternativeWorkDir` to the directory holding the bootstrap
   (`Bin/Win64MasterMasterSteamPGO` for KCD2).
3. Drive all initialisation from `GameModule::WhenReady(L"<GameModule>.dll", …)`.
4. Resolve patterns against the game module, not the executable — its preferred base
   is typically `0x180000000`, and it is relocated by ASLR at runtime, so never
   hardcode a base.
