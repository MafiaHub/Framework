# MafiaHub Framework Technical Context

## Development Environment

### Languages
- **C++17**: Primary development language
- **Lua 5.4**: Scripting language for game logic
- **CMake 3.20+**: Build system

### Build Tools
- **CMake**: Cross-platform build system generation
- **Visual Studio 2022**: Recommended IDE for Windows development
- **CLion**: Alternative IDE with good support for the project

### Version Control
- **Git**: Source code management
- **GitHub**: Repository hosting, issue tracking, and collaboration

## Core Technologies

### Networking
- **SLikeNet**: Networking library for reliable and unreliable communication
- **HTTP Library**: For RESTful API communication

### User Interface
- **Ultralight**: HTML/CSS renderer for complex UI
- **Dear ImGui**: Immediate mode GUI for tools and debugging interfaces
- **FreeType**: Font rendering

### Graphics
- **SDL2**: Cross-platform window management and input handling
- **DirectX**: Windows rendering support
- **OpenGL**: Cross-platform rendering support

### Data Management
- **JSON**: Data serialization format
- **SQLite**: Local database storage (when needed)

### Debugging and Profiling
- **Sentry**: Error reporting and monitoring
- **Optick**: Performance profiling
- **StackWalker**: Stack trace analysis

### Miscellaneous
- **GLM**: Mathematics library
- **spdlog**: Fast logging library
- **cppfs**: Filesystem operations
- **fmt**: String formatting
- **Steamworks/Galaxy**: Platform integration
- **Discord SDK**: Discord Rich Presence integration

## Dependencies
The framework relies on numerous third-party libraries, all of which are included in the `/vendors` directory or acquired through submodules. Key dependencies include:

```
glm                  - Mathematics
spdlog               - Logging
slikenet             - Networking
json                 - Data serialization
lua                  - Scripting
sdl2                 - Window management
sentry               - Error reporting
imgui                - GUI
ultralight           - Web-based UI
steamworks/galaxy    - Platform integration
discord              - Discord integration
```

## Technical Constraints

### Platform Support
- **Windows**: Primary development platform
- **Linux**: Supported for server and client
- **macOS**: Supported but with limited testing

### Performance Requirements
- Low latency for real-time multiplayer
- Efficient network bandwidth usage
- Minimal memory footprint to leave resources for the game

### Compatibility
- Must interoperate with various game engines and architectures
- Must support legacy game code integration
- Must handle different versions of operating systems

### Security
- Secure network protocols to prevent cheating
- Input validation to prevent exploits
- Secure handling of user data

## Development Setup

### Windows Setup
1. Install Visual Studio 2022 with CMake tools
2. Clone the repository with submodules
3. Open the directory in Visual Studio
4. Build the project

### Linux/macOS Setup
1. Install required development packages
2. Clone the repository with submodules
3. Configure with CMake
4. Build with the generated build files

### Adding Projects
1. Create/clone projects into `code/projects` directory
2. The project will be automatically included in the build
3. To exclude a project, create an empty `IGNORE` file in its root

## Project Structure
```
/code
  /framework       - Core framework code
    /src           - Source files organized by subsystem
  /projects        - Game-specific modification projects
/vendors           - Third-party dependencies
/cmake             - CMake scripts and modules
/builds            - Build outputs
/docs              - Documentation
```

## Versioning
The framework follows semantic versioning (MAJOR.MINOR.PATCH) as defined in the VERSION file.

## Testing
The framework includes unit tests and integration tests that can be run with:
```
cmake --build build --target RunFrameworkTests
``` 
