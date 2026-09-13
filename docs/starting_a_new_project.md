# Starting a New Project

A practical order of operations for bringing up a new multiplayer mod on the framework, from an empty folder to two players seeing each other move.

This guide is about **sequence** - what to build first, and what not to build at all. `docs/project_architecture.md` is the normative reference for *how* the code is shaped; it is cited here rather than repeated. Every snippet below is taken from a project that currently builds.

---

## 0. Before you write anything

Three properties of the target game decide the whole shape of the project. Answer them first, because they are expensive to change later.

**Architecture.** Is the game 32- or 64-bit? The client is a DLL loaded into the game, so it inherits that. The server is a standalone executable and is normally 64-bit. M2O's client is 32-bit and its server 64-bit; M3O is 64-bit on both. This decides the guards in `code/CMakeLists.txt` and it means client and server can never share a compiled object - only `shared/` source.

**Injection.** How does your DLL get into the process? A launcher that starts the game suspended and injects (M2O, M3O, VantaMP), a proxy/host DLL the game loads on its own (VantaMP ships `host_stubs/` for `eax` and `vorbis`), or a loader the game already supports. Decide now; it determines whether you need `code/launcher/`.

**The tick.** What is the game's own update loop, and where can you attach to it? Every mod needs one call site per frame and one at shutdown. Engines with a module system make this easy - Mafia II/III expose `C_TickedModule`, which M2O and M3O both use. Otherwise you hook the render present, the main loop, or a per-frame engine function. **Find this before you start**, because the shell is built around it.

You also need a signature-scanning workflow for the game binary. That work - finding `patterns` - usually dominates the first weeks and is unrelated to the framework.

---

## 1. The folder and the build

A project is registered by existing. `code/CMakeLists.txt` globs `projects/*` and adds each one unless it contains a file named `IGNORE`:

```cmake
file(GLOB projects "projects/*")
foreach (proj ${projects})
    get_filename_component(proj_name ${proj} NAME)
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/projects/${proj_name}/IGNORE")
        add_subdirectory("projects/${proj_name}")
    endif ()
endforeach ()
```

So: `code/projects/newmp/`, and four CMake files.

**`CMakeLists.txt`** (project root):

```cmake
cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(NewMP CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")
include_directories(${PROJECT_SOURCE_DIR}/code/framework/src/)

add_subdirectory(code)
```

**`code/CMakeLists.txt`** - the architecture gate and the version stamp:

```cmake
include(UpdateGitHash)
update_git_version(NewMP "${CMAKE_CURRENT_SOURCE_DIR}/shared/version.cpp.in" "${CMAKE_BINARY_DIR}/newmp_version.cpp")

include_directories(${CMAKE_SOURCE_DIR}/code/framework ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CL_64)                 # 64-bit game; use `MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4` for a 32-bit one
    add_subdirectory(client)
    add_subdirectory(launcher)
endif()

add_subdirectory(server)
```

**`code/client/CMakeLists.txt`** - a shared library linking `Framework` and `FrameworkClient`:

```cmake
file(GLOB_RECURSE NEWMP_CLIENT_FEATURES CONFIGURE_DEPENDS "src/features/*.cpp")

set(NEWMP_CLIENT_FILES
    src/main.cpp
    src/core/application.cpp
    src/core/application_module.cpp
    src/core/boot/mod_init.cpp
    src/core/boot/render_device.cpp
    src/sdk/patterns.cpp
    ${NEWMP_CLIENT_FEATURES}
)

add_library(NewMPClient SHARED ${NEWMP_CLIENT_FILES} ${CMAKE_BINARY_DIR}/newmp_version.cpp)
target_include_directories(NewMPClient PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(NewMPClient Framework FrameworkClient)
target_link_options(NewMPClient PRIVATE /MANIFEST:NO)
```

**`code/server/CMakeLists.txt`** - an executable linking `Framework` and `FrameworkServer`:

```cmake
file(GLOB_RECURSE NEWMP_SERVER_FEATURES CONFIGURE_DEPENDS "src/features/*.cpp")

add_executable(NewMPServer src/main.cpp src/core/server.cpp ${NEWMP_SERVER_FEATURES} ${CMAKE_BINARY_DIR}/newmp_version.cpp)
target_include_directories(NewMPServer PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(NewMPServer Framework FrameworkServer)

if(WIN32)
    target_compile_definitions(NewMPServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
```

The shell, `sdk/` and `game/` sources stay listed explicitly - they change rarely. Feature sources are globbed, so **adding a feature never edits a build file** (§2.1 of the reference).

Build from the CLI only, via `builds\build.bat NewMPClient 64` / `builds\build.bat NewMPServer 64`. Never `cmake --build`, never an ad-hoc build directory.

---

## 2. The entry point

**Client** - `client/src/main.cpp`. Order matters: memory protection, logging, crash reporter, hooking library, pattern table, patterns, then run the registered `InitFunction`s and enable hooks. Nothing else happens here.

```cpp
#include <utils/safe_win32.h>       // must be first - winsock conflicts otherwise

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    MarkMemoryRW((uint8_t *)((uintptr_t)GetModuleHandle(NULL)));

    NewMP::Core::gProjectPath = Framework::Utils::StringUtils::WideToNormal(projectPath);
    Framework::Logging::GetInstance()->SetLogName("NewMP");
    Framework::Logging::GetInstance()->SetLogFolder(NewMP::Core::gProjectPath + "\\logs");

    MH_Initialize();
    hook::set_base();
    hook::load_pattern_table(NewMP::Core::gProjectPath + "\\newmp.patterns");
    SDK::Patterns::InitPatterns();

    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}
```

`hook::load_pattern_table` seeds addresses from a table built ahead of time so the client doesn't rescan the image every launch; anything missing or stale falls back to a scan, so a bad table only costs the scan it was meant to save.

**Server** - `server/src/main.cpp`. Fill `InstanceOptions`, `Init`, `Run`, `Shutdown`:

```cpp
int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions opts;
    opts.bindHost = "0.0.0.0";  opts.bindPort = 27015;
    opts.webBindHost = "0.0.0.0"; opts.webBindPort = 27016;
    opts.maxPlayers = 32;
    opts.modName = "NewMP"; opts.modSlug = "newmp_server"; opts.modVersion = NewMP::Version::rel;
    opts.gameName = "<Game>"; opts.gameVersion = "1.0.0";
    opts.enableSignals = true;
    opts.argc = argc; opts.argv = argv;

    NewMP::Server server;
    if (const auto result = server.Init(opts); !result) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to start: {}", result.GetError().message);
        return 1;
    }
    server.Run();
    server.Shutdown();
    return 0;
}
```

That is a working server: it binds, exposes its HTTP endpoints, runs the scripting engine, talks to the masterlist, and accepts connections. You have not written a feature yet.

---

## 3. The SDK beachhead

You need very little `sdk/` to reach first light. Resist the urge to transcribe the whole engine before the mod runs.

The minimum is: `patterns.{h,cpp}` (the `SDK::Patterns` struct and `InitPatterns()`), the type your tick hook needs, and the render device types if you are hooking present. M3O reached a running client with about a dozen SDK headers.

Two rules apply from the first line (both from `AGENTS.md`, non-negotiable):

- **Never null-check a resolved pattern address.** `InitPatterns()` validates at scan time and fails loudly there. If the mod is running, every `gPatterns.*` entry is valid; a zero test on one is a branch that can never be taken.
- **Never write outside the repository.** Not the game install, not `user.cfg`, not saves. Reading for diagnosis is fine. Config the mod needs lives in the mod.

---

## 4. The shell

Four files, and then you stop touching it.

```
client/src/
  main.cpp
  core/
    application.{h,cpp}          : Framework::Integrations::Client::Instance
    application_module.{h,cpp}   adapter to the game's tick; owns Init/Update/Shutdown timing
    boot/
      mod_init.cpp               the hook that creates the ApplicationModule
      render_device.cpp          captures device/HWND; drives Render and WndProc
    feature_registry.cpp         constructs every service - one line per feature
```

**`Application`** starts as almost nothing - M3O's is fifteen lines:

```cpp
namespace NewMP::Core {
    class Application: public Framework::Integrations::Client::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void PreShutdown() override;
        void ModuleRegister(Framework::Scripting::Engine *engine) override;
    };
    extern std::unique_ptr<Application> gApplication;
}
```

**`ApplicationModule`** is where the game's loop meets the framework. It calls `Init` once the renderer exists, `Update` per tick, `Shutdown` on teardown:

```cpp
void ApplicationModule::OnSysInit(SDK::I_TickedModuleCallEventContext &) {
    Framework::Integrations::Client::InstanceOptions opts;
    opts.useRenderer = true;
    opts.useImGUI    = true;
    opts.usePresence = false;
    opts.rendererOptions = rendererOptions;
    opts.gameName = "<Game>"; opts.gameVersion = "1.0.0";
    opts.modSlug  = "newmp";  opts.modVersion  = NewMP::Version::rel;

    if (const auto result = gApplication->Init(opts); !result) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Init failed: {}", result.GetError().message);
    }
}

void ApplicationModule::OnTick(SDK::I_TickedModuleCallEventContext &) {
    if (gApplication && gApplication->IsInitialized()) { gApplication->Update(); }
}
```

Keep `Application` and `ApplicationModule` separate. One talks to the framework, the other to the game loop; merging them couples your lifecycle to the engine's and makes both untestable.

Boot hooks live in `core/boot/`, not in a feature folder - they belong to the shell. This is the only place hook files sit outside `features/`.

**Milestone 1: the client attaches, logs a line, and draws an ImGui window.** Nothing is networked yet. Do not proceed until this is stable across a game restart.

---

## 5. The first feature: players

Everything from here is a feature folder (§2.3 of the reference). The first one is always the player, because it is what proves the whole pipeline.

```
shared/features/player/
  player_entity.h
client/src/features/player/
  player.{h,cpp}
  player_service.{h,cpp}
  player_hooks.cpp
server/src/features/player/
  player_service.{h,cpp}
```

**The wire contract, defined once and compiled into both sides:**

```cpp
namespace NewMP::Shared::Features {
    class PlayerEntity : public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "NewMP::Player";
        static constexpr float kStreamRange = 250.0f;
        static constexpr float kWorldMin = -10000.0f, kWorldMax = 10000.0f;
        static constexpr int kPosBits = 24;                 // ~1.2 mm over a 20000-unit span

        std::string nickname;

        void OnSerializeConstruction(Replication::FieldSerializer &f) override { /* spawn metadata */ }
        void SerializeTransform(Replication::FieldSerializer &f) override {      // high-frequency, unreliable
            f.FloatBits(position.x, kWorldMin, kWorldMax, kPosBits);
            f.FloatBits(position.y, kWorldMin, kWorldMax, kPosBits);
            f.FloatBits(position.z, kWorldMin, kWorldMax, kPosBits);
        }
        void SerializeFields(Replication::FieldSerializer &f) override { /* low-frequency reliable deltas */ }
    };
}
```

The three-way split is the single most important decision in the file. Spawn metadata goes in construction, the transform on the unreliable channel, everything else as reliable deltas. A rarely-changing string in `SerializeTransform` costs bandwidth on every idle player, every tick, forever.

**Registration.** Type ids are CRC32 of `kTypeName`, so order does not matter - but both sides must register the *same set*, which is why the list lives in shared code:

```cpp
// shared/register_entities.cpp
void RegisterEntities() {
    auto &factory = Framework::Networking::Replication::EntityRegistry::Get();
    factory.Register<PlayerEntity>(PlayerEntity::kTypeName);
}
```

Call it once per side before connecting. Keep it an explicit list rather than a static self-registrar in each header: static registrars in a static library get stripped by the linker when nothing references the translation unit, and the symptom is a null replica at runtime, not a link error.

**The client half** subclasses the shared entity and drives the native object:

```cpp
class Player final : public Shared::Features::PlayerEntity {
  public:
    void OnConstructed() override;              // remote appeared - spawn a ped
    void OnDeserialized(bool transformUpdated) override;
    void DeallocReplica(MafiaNet::Connection_RM3 *) override;
    void Frame();                               // per-frame drive

    bool isLocalPlayer = false;
};
```

The owner captures its native state upstream; remotes apply what arrives. Write that asymmetry in the class comment - it is the thing every reader needs first.

**The server half** creates a player entity per connection and registers it as that connection's viewer:

```cpp
void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) {
    _players.Add(data.guid, data.nickname);
}
void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) { _players.Remove(guid); }
```

The avatar is also the viewer: its position and stream range decide what that connection is told about. A player whose entity is not the registered viewer sees everything, everywhere.

**Milestone 2: two clients connect and see each other move.** This is the real first light. Everything after it is more features of the same shape.

---

## 6. Events, RPC and scripting

**Events come from hooks, not polling** (§5). A local game event is observed by hooking the call site that produces it; the hook stays thin and forwards to its own feature's service, which resolves entities, raises the script event and owns the broadcast:

```cpp
// player_hooks.cpp - detects and forwards, nothing else
char __fastcall Player_Died_Hook(void *ped, void *edx) {
    const char r = Player_Died_Original(ped, edx);
    gApp->Players().NotifyDied(reinterpret_cast<SDK::C_Ped *>(ped));
    return r;
}
static InitFunction init([]{ MH_CreateHook(gPatterns.C_Ped__Died, ...); }, "PlayerHooks");
```

Diffing a *replicated* value on apply is correct and is not polling. Reading *local* game state on a timer to infer that something happened is, when a hookable call site exists.

**RPC** is typed and lives on the peer:

```cpp
peer->RegisterRPC<Shared::Features::PlayerDeath>([](const auto &msg, MafiaNet::Packet *p) { ... });
peer->BroadcastRPC(msg);
peer->SendRPC(msg, guid);
```

**Scripting** is added per feature, in `<stem>_scripting.{h,cpp}` inside the feature folder, registered from that feature's `Install()`. A binding validates arguments and calls its service - it must not name an `SDK::` type, a replica or the peer. Register its metadata in the catalog at the same time, or it will not appear in the generated TypeScript contract.

Add scripting **after** milestone 2, not before. A binding for a feature that does not work yet is a guess at an API.

---

## 7. The state machine

Add `Framework::Utils::States::Machine` when the connection flow stops fitting in `Application`. The canonical set:

```
Initialize -> GameSetup -> MainMenu -> SessionConnection -> SessionConnected
           -> SessionDisconnection -> Shutdown     (+ an offline/debug state)
```

One `IState` per file in `core/states/`, ids in `states.h` as an unscoped `enum StateIds : int32_t` - the framework's `GetId()` returns `int32_t` and a scoped enum would force a cast in every state.

A state's `OnUpdate` drives *transitions*. It must not accumulate gameplay state; that belongs to a service the state starts and stops.

---

## 8. Order of work

| # | Milestone | Done when |
|---|---|---|
| 1 | Build skeleton | `builds\build.bat NewMPServer 64` produces a server that binds a port |
| 2 | Injection + entry | The DLL loads and writes a log line |
| 3 | Patterns | `InitPatterns()` resolves against the shipped game build |
| 4 | Shell | `Application::Init` succeeds; an ImGui window draws |
| 5 | **Player feature** | Two clients see each other move |
| 6 | Second feature (vehicles) | Proves the layout generalises |
| 7 | State machine | Connect / disconnect / reconnect is clean |
| 8 | Scripting | A resource can move a player |
| 9 | Launcher, assets, voice | Framework features you switch on, not build |

Milestones 1-4 are game-specific reverse engineering and take the longest. 5 onward are framework work and go quickly, provided the layout is right from the start - which is the whole reason to read `docs/project_architecture.md` before milestone 5, not after.

---

## 9. Do not build these

The framework already provides them. Reaching for your own is the most common way a new project accumulates work it does not need:

networking and reconnect · entity replication, interest grid and streaming · typed RPC · JS/TS scripting on both sides · CEF and ImGui · chat · proximity voice (the server relays opaque Opus frames without decoding) · asset download · Discord presence · masterlist registration · HTTP endpoints · console and chat commands · crash reporting · signature scanning and hooking · pattern tables · state machine · fiber job system · profiler · logging · JSON · persistent config.

---

## 10. The rules that keep it from rotting

Four, from `docs/project_architecture.md`. They cost nothing on day one and are expensive to retrofit at feature fifty:

1. **State has an owner.** Nothing mutable survives a call inside an anonymous namespace. It lives on a service with `Update()` and `Reset()`, so a reconnect cannot leak it. (§4)
2. **Local events come from hooks.** Polling local game state to infer an event is forbidden where a call site exists to hook. (§5)
3. **Role is in the filename.** `_hooks` may not send RPC; `_scripting` may not name an `SDK::` type. Both are one grep, which is what lets features be folders. (§1, §7.6)
4. **One folder per feature, always** - one file or five, hooks and bindings included. There is no size at which a feature "earns" a directory. (§2.3)

Read §13 of the reference before the first review; it is the checklist a reviewer or an agent will run against the project.
