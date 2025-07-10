# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MafiaHub Framework is a comprehensive C++ framework for building multiplayer game modifications. It provides a foundation for creating synchronized multiplayer experiences with networking, ECS (Entity Component System), scripting, GUI, and other essential components.

## Build System

### Building the Project

The project uses CMake as the build system and supports Windows, Linux, and macOS.

**macOS/Linux:**
```bash
# Configure CMake project
cmake -B build

# Build framework
cmake --build build

# Run framework tests
cmake --build build --target RunFrameworkTests
```

**Windows:**
- Use Visual Studio 2022 with CMake tools installed
- Open the repository folder in Visual Studio for automatic setup
- Enable "Show All Files" in Solution Explorer to see project files in code/projects

### Project Structure

- `code/projects/` - Multiplayer modification projects are cloned here and automatically discovered
- `code/framework/` - Core framework source code
- `vendors/` - Third-party dependencies
- `scripts/` - Build and maintenance scripts

To exclude a project from compilation, create an empty file called `IGNORE` in the project's root.

## Architecture

### Core Components

The framework uses a modular architecture with several key systems:

1. **World Engine** (`Framework::World::Engine`) - ECS-based world management using Flecs
2. **Networking** (`Framework::Networking::NetworkPeer`) - Handles client-server communication
3. **Scripting** (`Framework::Scripting::Engine`) - Lua scripting system for game logic
4. **GUI Manager** (`Framework::GUI::Manager`) - UI system using Ultralight and Dear ImGui
5. **Core Modules** (`Framework::CoreModules`) - Central registry for accessing system singletons

### Key Patterns

- **ECS Architecture**: Uses Flecs ECS for entity management and world streaming
- **Module System**: Components are registered through a central CoreModules singleton
- **RPC System**: Network communication uses RPC macros `FW_SEND_COMPONENT_RPC` and `FW_SEND_COMPONENT_RPC_TO`
- **Integration Layer**: Server and client integrations provide complete game server/client implementations

### Important File Locations

- `code/framework/src/core_modules.h` - Central module registry
- `code/framework/src/world/engine.h` - ECS world engine interface
- `code/framework/src/integrations/server/instance.h` - Server integration implementation
- `code/framework/src/integrations/client/instance.h` - Client integration implementation

## Development

### Key Dependencies

- **Flecs** - Entity Component System
- **Lua 5.4** - Scripting engine
- **SlikeNet** - Networking library
- **Ultralight** - Web-based UI rendering
- **Dear ImGui** - Immediate mode GUI
- **spdlog** - Logging
- **nlohmann/json** - JSON parsing
- **OpenSSL** - Cryptography

### Code Organization

- Framework is split into client (`FrameworkClient`) and server (`FrameworkServer`) libraries
- Platform-specific code is conditionally compiled (Windows-specific features in `#ifdef WIN32`)
- Shared utilities and core systems are in the base `Framework` library

### Testing

Run tests with:
```bash
cmake --build build --target RunFrameworkTests
```

### Multi-project Development

Clone multiplayer projects into `code/projects/` for automatic discovery:
```bash
mkdir -p code/projects
cd code/projects
git clone https://github.com/MafiaHub/MafiaMP.git
```

## Important Notes

- The framework uses C++17 standard
- Windows builds require Visual Studio 2022
- Projects use MafiaHub OSS license
- Framework provides hooks for MafiaHub Services integration
- Server instances support HTTP endpoints and command processing
- Client instances support game integration and graphics rendering