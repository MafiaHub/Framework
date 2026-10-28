# AGENTS.md

This file provides guidance to AI coding agents (Codex, Claude Code, Cursor and others) when working
with code in this repository. It is the only instructions file: `CLAUDE.md` imports it, so edit this
one.

## Non-negotiable rules

These are not suggestions. Do not weaken, re-litigate, or "defensively" work
around them.

### 1. Never null-check a resolved pattern address

Signature scanning is validated at scan time. `hook::get_pattern` and friends
resolve during `Patterns::InitPatterns()`, before anything uses the result. If
a signature does not match, that fails there and then — loudly, at startup.

**Therefore: if the mod is running, every `gPatterns.*` entry is valid.** A
zero/null test on one is dead code that can never be true. It buys nothing,
hides the real failure point, and adds a fake error path that has to be read,
reviewed, and maintained forever.

```cpp
// FORBIDDEN — dead branch, never taken
IInput *IInput::GetInstance() {
    if (gPatterns.SSystemGlobalEnvironment__Base == 0) {
        return nullptr;
    }
    return *reinterpret_cast<IInput **>(gPatterns.SSystemGlobalEnvironment__Base + 0x40);
}

// CORRECT
IInput *IInput::GetInstance() {
    return *reinterpret_cast<IInput **>(gPatterns.SSystemGlobalEnvironment__Base + 0x40);
}
```

The same applies to hook installation. Call `MH_CreateHook` on
`gPatterns.<Symbol>` directly; do not stage it through a local and test it, and
do not log a "was not resolved" warning that cannot fire.

This extends to anything else already guaranteed by construction: console
variable names verified against the binary, vtable slots proven against the
game's own call sites, and singletons whose lifetime the call site already
establishes. Guard genuine runtime conditions — a game-owned pointer that is
legitimately null at that moment, a bounds check, an index the caller supplies.
Never guard a fact.

**What makes it a fact is that the scan aborts.** `hook::get_pattern` and
friends `count(1)` internally, so a stale signature takes the DLL down at load
and nothing downstream can observe a zero. A project whose resolver *does not*
abort has no such guarantee, and there the zero is a genuine runtime condition:
`HogwartsMP` resolves every address through `Core::AobFirst`, which deliberately
logs a miss and returns null so one launch surfaces every stale AOB instead of
the first one killing the load. Its `SDK::gPatterns` fields are therefore
legitimately zero and MUST be checked — see the contract at the top of
`code/projects/hogwarts/code/client/src/sdk/patterns.h`. The rule is about
`gPatterns`; do not carry it across to a non-aborting resolver, and do not
"fix" a project by removing the guards a non-aborting resolver requires.

### 2. Never touch files outside the repository

Do not create, edit, or delete anything in the user's game install, home
directory, or any other user-space location. That includes `user.cfg`,
`system.cfg`, save games, and profile data. Reading them for diagnosis is fine;
writing is not. If a change needs to happen there, tell the user and let them
make it. Configuration the mod needs belongs in the mod, applied in-process.

## Project Overview

MafiaHub Framework is a C++ framework for building multiplayer game modifications. It provides networking, entity replication, scripting, GUI, and other essential components for synchronized multiplayer experiences.

## Build Commands

> **ALWAYS build from the CLI via `builds\build.bat <target> <arch>`.** This is the single
> supported way to build any target in this repo from the command line. It loads the matching
> `vcvars*.bat` and drives the canonical pre-configured build tree. Never invoke `cmake --build`
> directly and never create ad-hoc/temporary build directories. See the **Mandatory Windows
> build rule** below.

**macOS/Linux:**
```bash
cmake -B build              # Configure
cmake --build build         # Build
cmake --build build --target RunFrameworkTests  # Run tests
```

Set `FW_TEST_FILTER` to run only the test modules whose name contains one of its comma-separated
substrings (`FW_TEST_FILTER=voice,state_bag build/bin/FrameworkTests`; on Windows set it before
`builds\build.bat RunFrameworkTests 64`). A failed assertion prints the expression, both values and
`file:line`, and the run ends with a `> failures:` list, so the tail of the log is enough to act on.

**Windows:**
```bash
builds\build.bat <target> 64
```

For HogwartsMP, build the debug client and server with:
```bat
builds\build.bat HogwartsMPClient 64
builds\build.bat HogwartsMPServer 64
```

**Mandatory Windows build rule:** Always use `builds\build.bat`. Its canonical 64-bit debug build directory is `builds\build-64`, and its artifacts belong in that build's debug output. Never create or use ad-hoc CMake build directories (including any `codex-*`, `verify`, smoke, or temporary build folder) for repository builds. Do not invoke `cmake --build` directly when the build script can build the requested target. If the canonical build is broken, diagnose or repair it rather than creating a parallel build tree.

Or use Visual Studio 2022 with CMake tools installed and open the repository folder for automatic setup.

The Framework ships to projects as a prebuilt SDK (`cmake --install` of this tree; see
`cmake/FrameworkInstall.cmake`). `builds\build.bat FrameworkSdkSmoke 64`
installs it to `builds\build-64\sdk` and builds `code/tests/sdk-consumer` against that prefix alone.
Projects consume it through `cmake/FrameworkBootstrap.cmake` (`FW_SDK_ROOT`, `FW_SOURCE_DIR` or a
`FrameworkPin.cmake`); the targets are `Framework::Framework`, `Framework::Client`, `Framework::Server`,
`Framework::Loader`.

## Code Intelligence

Configuring the tree writes `compile_commands.json` into the build directory. Use clangd (pinned in
`mise.toml`) for definitions, references and call hierarchies instead of grepping: it resolves the
`FW_*` macros, templates such as `RegisterRPC<T>` and overloads that text search cannot. Claude Code
gets it through the `clangd-lsp` plugin that `.claude/settings.json` enables.

clangd finds `build/compile_commands.json` on its own. On Windows the canonical tree is
`builds\build-64`, so add a `.clangd` (gitignored) at the repository root containing:

```yaml
CompileFlags:
  CompilationDatabase: builds/build-64
```

## Agent Hooks

`.claude/settings.json` registers two hooks (Node scripts in `.claude/hooks/`, Node pinned in
`mise.toml`) that enforce rules from this file instead of relying on agents to remember them:

- `guard.mjs` (before a tool runs) denies file writes outside the repository, temporary
  directories and `~/.claude` (non-negotiable rule 2), and CMake commands aimed at anything but the
  canonical `build/` tree, or any direct `cmake --build`/`cmake -B` on Windows.
- `format.mjs` (after an edit) runs the pinned clang-format on the lines just changed in a C/C++
  file under `code/framework/` (the scope of `scripts/format_codebase.sh`). Only those lines: existing formatting drift in the file is left alone.

Other agents do not run these hooks; the rules still apply to them as written.

## Project Structure

- `code/framework/` - Core framework source code split into three libraries:
  - `Framework` - Shared utilities and core systems
  - `FrameworkClient` - Client-specific features (rendering, Discord presence, asset downloading)
  - `FrameworkServer` - Server-specific features (HTTP endpoints, command processing, masterlist)
  - Structure and coding rules for these are normative in `docs/project_architecture.md`
  - Bringing up a new one: `docs/starting_a_new_project.md`
- `code/tests/` - Framework tests
- `vendors/` - Third-party dependencies that are not on vcpkg: MafiaNet, CEF, libnode, v8pp,
  FTL, steamworks, discord, and a handful of small hand-rolled libs. Four are kept here on
  purpose despite having ports - physfs (the port builds every archiver), tracy (no
  `TRACY_DELAYED_INIT`), imguizmo (port predates the ImGui 1.92 `AddPolyline` change) and
  miniaudio (port ships only the header). Each carries the reason in `vendors/CMakeLists.txt`.
  CEF is downloaded and unpacked into `vendors/cef` at configure time; set `FW_CEF_CACHE_DIR`
  (`-D` or an environment variable) to share one copy between checkouts and worktrees.
- `vcpkg.json` - everything else, pinned by `builtin-baseline`. vcpkg is fetched and
  bootstrapped at configure time by `cmake/VcpkgBootstrap.cmake`; override with
  `-DFW_VCPKG_ROOT=<path>` (not the `VCPKG_ROOT` env var, which `vcvars64.bat` hijacks).
  Triplets in `cmake/vcpkg-triplets/` pin the `/MD` runtime with static libs, release-only.

## Architecture

### Core Systems

1. **CoreModules** (`core_modules.h`) - Central singleton registry coupling all modules together
2. **Replication** (`networking/replication/`) - Native MafiaNet entity replication (ReplicaManager3 + RPC4) with an interest grid for streaming
3. **Networking** (`networking/network_peer.h`) - Client-server communication via MafiaNet
4. **Scripting** (`scripting/`) - JavaScript/TypeScript scripting for game logic (Server: libnode, Client: V8)
5. **GUI Manager** (`gui/manager.h`) - UI using CEF and Dear ImGui
6. **Job System** (`jobs/job_system.h`) - Opt-in fiber-based task scheduling using FTL
7. **Voice** (`voice/`) - Proximity voice chat; the server relays opaque Opus frames (RakVoice) without decoding, routed by `VoiceRouter`

### Integration Layer

The framework provides ready-to-use server and client implementations:

- **Server** (`integrations/server/instance.h`) - Complete game server with HTTP endpoints, command processing, scripting, and MafiaHub Services integration
- **Client** (`integrations/client/instance.h`) - Game client with rendering, Discord presence, asset downloading, and networking

Both expose virtual methods (`PostInit`, `PostUpdate`, `PreShutdown`, `ModuleRegister`) for game-specific customization.

### Key Patterns

- **RPC System**: Typed, on the peer — `RegisterRPC<T>(handler)` to receive, `BroadcastRPC<T>(payload)` and `SendRPC<T>(payload, guid)` to send (`networking/network_peer.h`). Payload structs live in the project's `shared/`, one per header.
- **Module Registration**: Access systems via `Framework::CoreModules::Get*()` static methods — `GetNetworkPeer()`, `GetReplication()`, `GetScriptingModule()`, `GetWebManager()`, `GetInput()`, `GetVoiceServer()` / `GetVoiceClient()`, `GetClientInstance()`
- **Replicated Entities**: Subclass `Framework::Networking::Replication::NetworkEntity`, declare a `kTypeName`, and register it with `EntityRegistry::Get().Register<T>(T::kTypeName)`. Type ids are a CRC32 of the name, so registration order is irrelevant — but client and server MUST register the same set, so keep the list in shared code.
- **Serialization**: Split three ways — `OnSerializeConstruction` (spawn metadata), `SerializeTransform` (high-frequency, unreliable), `SerializeFields` (low-frequency reliable deltas).

## Code Style

- Uses `.clang-format` (LLVM-based) - run `scripts/format_codebase.sh` to format
- Namespaces: Use `namespace Framework::SubModule {` with closing comment `} // namespace Framework::SubModule`
- Header guards: Use `#pragma once`
- Variables: `_` prefix for private members, camelCase naming
- Classes: PascalCase, mark as `final` when possible

## Commit Format

Format: `Module: Brief commit description`
- Wrap messages at 72 characters
- Rebase on `develop` branch
- Split changes into atomic commits

## Version Semantics

- **PATCH**: Changes not affecting peer sync or scripting layer
- **MINOR**: Scripting layer changes
- **MAJOR**: Netcode, shared ECS modules, or sync flow changes (requires both client and server update)

## Key Dependencies

- **FTL** - Fiber Tasking Library for job system (v2.1.0)
- **libnode/V8** - JavaScript scripting (Server uses libnode for full Node.js APIs, Client uses V8 for sandboxed execution)
- **MafiaNet** - Networking (MafiaHub's fork of RakNet/SLikeNet)
- **CEF** - Web-based UI (Chromium Embedded Framework)
- **Dear ImGui** - Immediate mode GUI
- **Tracy** - Frame profiler (wrapped by `utils/profiler.h` FW_PROFILE_* macros; toggle with `FW_PROFILING`)
- **spdlog** - Logging
- **nlohmann/json** - JSON parsing
