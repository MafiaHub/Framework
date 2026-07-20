# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project Overview

MafiaHub Framework is a C++ framework for building multiplayer game modifications. It provides networking, ECS (Entity Component System), scripting, GUI, and other essential components for synchronized multiplayer experiences.

## Build Commands

**macOS/Linux:**
```bash
cmake -B build              # Configure
cmake --build build         # Build
cmake --build build --target RunFrameworkTests  # Run tests
```

**Windows:**
```bash
cmake -B build              # Configure
cmake --build build         # Build
cmake --build build --target RunFrameworkTests  # Run tests
```

Or use Visual Studio 2022 with CMake tools installed and open the repository folder for automatic setup.

### CMake Source Discovery

Sources in `code/framework/src/scripting/builtins/` are discovered automatically
by `code/framework/CMakeLists.txt` using `file(GLOB ... CONFIGURE_DEPENDS)`. Do
not add individual builtin `.cpp` paths to `FRAMEWORK_SRC`. When adding,
deleting, or renaming a builtin, keep its headers, includes, registrations, and
tests in sync, then run a clean CMake configure to verify the discovered source
set. This avoids stale paths such as the former `environment.cpp` entry after it
was renamed to `execution_environment.cpp`.

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
