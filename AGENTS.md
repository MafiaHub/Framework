# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

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

### 2. Never touch files outside the repository

Do not create, edit, or delete anything in the user's game install, home
directory, or any other user-space location. That includes `user.cfg`,
`system.cfg`, save games, and profile data. Reading them for diagnosis is fine;
writing is not. If a change needs to happen there, tell the user and let them
make it. Configuration the mod needs belongs in the mod, applied in-process.

## Project Overview

MafiaHub Framework is a C++ framework for building multiplayer game modifications. It provides networking, ECS (Entity Component System), scripting, GUI, and other essential components for synchronized multiplayer experiences.

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

## Project Structure

- `code/framework/` - Core framework source code split into three libraries:
  - `Framework` - Shared utilities and core systems
  - `FrameworkClient` - Client-specific features (rendering, Discord presence, asset downloading)
  - `FrameworkServer` - Server-specific features (HTTP endpoints, command processing, masterlist)
- `code/projects/` - Multiplayer projects (auto-discovered, create `IGNORE` file to exclude)
- `code/tests/` - Framework tests
- `vendors/` - Third-party dependencies

## Architecture

### Core Systems

1. **CoreModules** (`core_modules.h`) - Central singleton registry coupling all modules together
2. **Replication** (`networking/replication/`) - Native MafiaNet entity replication (ReplicaManager3 + RPC4) with an interest grid for streaming
3. **Networking** (`networking/network_peer.h`) - Client-server communication via MafiaNet
4. **Scripting** (`scripting/`) - JavaScript/TypeScript scripting for game logic (Server: libnode, Client: V8)
5. **GUI Manager** (`gui/manager.h`) - UI using CEF and Dear ImGui
6. **Job System** (`jobs/job_system.h`) - Opt-in fiber-based task scheduling using FTL

### Integration Layer

The framework provides ready-to-use server and client implementations:

- **Server** (`integrations/server/instance.h`) - Complete game server with HTTP endpoints, command processing, scripting, and MafiaHub Services integration
- **Client** (`integrations/client/instance.h`) - Game client with rendering, Discord presence, asset downloading, and networking

Both expose virtual methods (`PostInit`, `PostUpdate`, `PreShutdown`, `ModuleRegister`) for game-specific customization.

### Key Patterns

- **RPC System**: Use `FW_SEND_COMPONENT_RPC(rpc, ...)` and `FW_SEND_COMPONENT_RPC_TO(rpc, guid, ...)` for network communication
- **Module Registration**: Access systems via `Framework::CoreModules::Get*()` static methods
- **Entity Factories**: Use `PlayerFactory` and `StreamingFactory` for entity creation

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
