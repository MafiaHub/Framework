# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
- Use Visual Studio 2022 with CMake tools installed
- Open the repository folder in Visual Studio for automatic setup

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
2. **World Engine** (`world/engine.h`) - ECS-based world management using Flecs with streaming support
3. **Networking** (`networking/network_peer.h`) - Client-server communication via SlikeNet
4. **Scripting** (`scripting/engine.h`) - Lua 5.4 scripting for game logic
5. **GUI Manager** (`gui/manager.h`) - UI using Ultralight and Dear ImGui

### Integration Layer

The framework provides ready-to-use server and client implementations:

- **Server** (`integrations/server/instance.h`) - Complete game server with HTTP endpoints, command processing, scripting, and MafiaHub Services integration
- **Client** (`integrations/client/instance.h`) - Game client with rendering, Discord presence, asset downloading, and networking

Both expose virtual methods (`PostInit`, `PostUpdate`, `ModuleRegister`) for game-specific customization.

### Key Patterns

- **RPC System**: Use constructor-based RPC calls with direct method invocations:
  ```cpp
  // Construct and send an RPC
  Framework::World::RPC::SetTransform rpc(transform);
  net->SendRPC(rpc, guid);

  // Or use templated helpers
  net->sendRPC<Framework::World::RPC::SetTransform>(guid, transform);
  net->sendGameRPC<Framework::World::RPC::SetFrame>(engine, entity, frame);
  ```
- **Message Handler Registration**: Use the fluent router API:
  ```cpp
  auto r = net->router();
  r.on<ClientHandshake>().handle(this, &Instance::OnClientHandshake);
  r.onRPC<EmitLuaEvent>().handle(this, &Instance::OnEmitLuaEvent);
  r.onGameRPC<SetTransform>().handle(this, &Instance::OnSetTransform);
  ```
- **Module Registration**: Access systems via `Framework::CoreModules::Get*()` static methods
- **Entity Factories**: Use `PlayerFactory` and `StreamingFactory` for entity creation

See [docs/NETWORKING_API_MIGRATION_GUIDE.md](docs/NETWORKING_API_MIGRATION_GUIDE.md) for complete API documentation and migration details.

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

## Key Dependencies

- **Flecs** - Entity Component System
- **Lua 5.4** - Scripting
- **SlikeNet** - Networking
- **Ultralight** - Web-based UI
- **Dear ImGui** - Immediate mode GUI
- **spdlog** - Logging
- **nlohmann/json** - JSON parsing
