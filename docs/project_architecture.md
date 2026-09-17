# Project Architecture Reference

Normative structure and coding rules for every multiplayer project under `code/projects/`.

This document is the single source of truth for *how a project is shaped*. `AGENTS.md` remains the source of truth for the non-negotiable safety rules (never null-check a resolved pattern address; never write outside the repository) and those rules win on any conflict.

**Where this comes from.** M2O (`code/projects/m2o`) is the baseline and supplies most of the worked examples - its layering, its thin-hook contract, its serialization split. Two things are taken from elsewhere because they are better: KCD2MP's per-feature folder and its feature-owner interface (`Install` / `Update` / `Reset` / `Notify*`). And one thing is in no project yet: grouping **all** of a feature's code - hooks, entity, service, bindings, RPC payloads - in one folder per side. Every project today groups by role instead, which is why §2.5 is a migration table rather than a compliance report, and why M2O appears in it as often as anyone.

**Bringing up a new project** is a different question from shaping an existing one - see `docs/starting_a_new_project.md` for the order of operations, from empty folder to two players seeing each other. This document is the reference it cites.

**Rule strength.** `MUST` / `MUST NOT` are mechanical - a reviewer or an agent can decide compliance from the code alone. `SHOULD` admits a documented exception; write the reason in a one-line comment at the exception site.

**Scope.** The rules apply to mod code - everything under `client/src` and `server/src` except `sdk/`, plus `shared/`. They do **not** apply to `sdk/`, which is a transcription of the game binary and, in some projects, contains imported engine headers verbatim (`hogwarts/code/client/src/sdk/ue/`). Every metric quoted below excludes `sdk/` for that reason. One project sits outside the target architecture and needs a migration decision before this document is applied to it: `cybermp` (no `shared/rpc`, no scripting layer yet).

---

## 1. Layers are a dependency rule, not a folder layout

A mod is four layers. They are real, and the dependency direction between them is non-negotiable - but they are **not** directories. Grouping by layer means one feature is scattered across six folders, and at the scale these projects already reach (M2O today: 40 hook files, 27 entity modules, 15 client bindings, 18 server bindings, 48 RPC headers) that makes a feature impossible to see. Code is grouped by **feature** (§2, §7); the layer a file belongs to is carried by its **name suffix**, and enforced by what it is allowed to include.

```
scripting   JS/TS surface                    -> its own feature, shared
core        mod behaviour                    -> game, sdk, shared, other features' public headers
game        game-facing systems              -> sdk, shared
sdk         reverse-engineered game bindings -> shared
shared      wire types, registries, constants -> framework only
```

| Role | File suffix | Owns | Must never name |
|---|---|---|---|
| **Bindings** | `_scripting.{h,cpp}` | Translation of the feature's service API into JS, and its catalog metadata. | An `SDK::` type, a replica, a network peer. |
| **Service** | `_service.{h,cpp}` | The feature: its state, its policy, its script events, its broadcasts. | - |
| **Entity** | `<feature>.{h,cpp}`, `_entity.h` | The replicated type and, client-side, the native object it drives. | Another feature's service. |
| **Hooks** | `_hooks.cpp` | Detecting a game event and forwarding it, thin. | An RPC send, a replica, scripting. |
| **Handlers** | `_handlers.cpp` | Server RPC entry points; stamps the authoritative actor. | An `SDK::` type. |
| **Native override** | `game/overrides/*` | A class replacing game behaviour by deriving from an `SDK::` type. | Networking, scripting. |

Two absolutes on top of the table: **`sdk/` never includes framework or networking headers**, and **`shared/` never includes `sdk/`**. Both are one-line greps.

**Test for a misplaced file.** If `*_scripting.cpp` names an `SDK::` type, or `*_hooks.cpp` sends an RPC, the layering is broken - regardless of which folder the file sits in. Move the policy into the service, not the dependency into the file.

---

## 2. Canonical folder layout

Client and server are separate targets, often for different architectures - M2O's client is 32-bit only and its server 64-bit only - so the top-level split by side is forced by the build and stays. Inside each side, code is grouped by feature.

```
code/
  CMakeLists.txt
  client/
    CMakeLists.txt
    data/                     <project>.patterns, shipped data files
    src/
      main.cpp                DLL entry / injection bootstrap only
      core/                   the shell (see 2.2)
        application.{h,cpp}   the Framework::Integrations::Client::Instance subclass
        application_module.{h,cpp}  the game's own tick/module interface adapter
        local_events.{h,cpp}  cross-feature and shell-level local events (§5.2)
        crash_reporting.{h,cpp}
        boot/                 shell hooks only: mod entry, render device capture, tick attach
        feature_registry.cpp  constructs every service - one line per feature
        states/               lifecycle states + states.h (the id enum)
        ui/                   shell UI only: escape menu, toast host, debug host
      features/<feature>/     everything for one feature on this side (see 2.3)
      game/                   cross-feature game infrastructure (see 2.2)
        entities/             factories, spawners, slot managers
        helpers/              input translation, math, game-term utilities
        replicator/           local -> wire capture and interpolation
        overrides/            native overrides (classes deriving from SDK types)
      sdk/                    game bindings (see 2.4)
  server/
    CMakeLists.txt
    src/
      main.cpp
      core/
        server.{h,cpp}        the Framework::Integrations::Server::Instance subclass
        scripting_registry.{h,cpp}  engine bootstrap and the binding registration list
      features/<feature>/
  shared/
    features/<feature>/       entity definition + one header per RPC payload
    register_entities.cpp     includes every feature's *_entity.h, registers them all
    scripting_catalog.h       catalog handles; per-feature metadata is registered by the feature
    session_config.h
    version.h, version.cpp.in
  launcher/                   only if the game needs a patcher/loader
```

### 2.1 CMake

Each side globs its features rather than hand-listing them:

```cmake
file(GLOB_RECURSE <P>_CLIENT_FEATURES CONFIGURE_DEPENDS "src/features/*/*.cpp")
```

Shell, `game/` and `sdk/` sources stay explicitly listed - they change rarely and the ordering sometimes matters. Feature sources do not: adding a feature MUST NOT require editing `CMakeLists.txt`. M2O's `M2O_CLIENT_FILES` currently enumerates all 40 hook files, 27 modules and 15 binding files by hand, and every new feature is three more lines in a list nobody reads.

### 2.2 What is not a feature

Three things stay grouped by role, because they are not about any one feature:

- **The shell** (`core/`) - the `Application`/`Instance`, the engine tick adapter, the lifecycle state machine, crash reporting, and UI that hosts other UI. It starts and stops features; it does not implement any. `core/boot/` is the one place hook files live outside a feature folder: the mod entry hook, render-device capture, and whatever attaches the framework to the game's tick. These belong to no feature - they are what makes features possible. Everything else that hooks belongs to the feature it serves.
- **Cross-feature game infrastructure** (`game/`) - input translation, entity spawning, the replicator, math helpers, native overrides. The test is mechanical: **used by two or more features, it lives in `game/`; used by one, it lives in that feature's folder.**
- **`sdk/`** - a transcription of the game binary, organised the way the game is.

### 2.3 A feature folder

```
client/src/features/jukebox/
  jukebox.{h,cpp}             entity, client half - drives the native object
  jukebox_service.{h,cpp}     the feature owner (§7.4)
  jukebox_hooks.cpp           game hooks, thin
  jukebox_scripting.{h,cpp}   JS bindings + catalog metadata
  jukebox_ui.{h,cpp}          the feature's own panel, if it has one

server/src/features/jukebox/
  jukebox_service.{h,cpp}
  jukebox_handlers.cpp        client->server RPC entry points
  jukebox_scripting.{h,cpp}

shared/features/jukebox/
  jukebox_entity.h            replicated definition
  jukebox_use.h               one header per RPC payload
```

Filenames repeat the feature stem rather than being bare role names (`jukebox_service.cpp`, not `service.cpp`). The stutter buys unique basenames across the whole tree, which is what makes grep output, editor tabs, and compiler diagnostics readable at a hundred features. KCD2MP already does this (`core/modules/door/door_module.cpp`).

Not every feature has every file. A feature with no replicated state has no entity; one with no JS surface has no `_scripting`; one that only reacts to the game has only `_hooks.cpp` and a service. What a feature MUST NOT do is spill outside its folder - no `core/hooks/` bucket, no `scripting/` bucket, no entry in a shared `builtins/` folder.

**The folder is unconditional.** A feature that is one file today still gets its own directory: `features/season/season_service.{h,cpp}`, never a loose `features/season_service.{h,cpp}` beside the other feature folders. There is no size threshold at which a feature "earns" a directory, for three reasons:

- **Growth is free.** The one-file feature that acquires a hook next month adds a file. With a threshold it instead moves, which rewrites every include path that named it, splits its git history at exactly the moment it got interesting, and has to be noticed by someone first.
- **One shape.** Every path rule, glob, and grep in this document is written against `features/<stem>/...`. A conditional layout means each of them needs a second case, and `src/features/*/*.cpp` silently stops seeing the loose files.
- **No judgment call.** "Is this big enough for a folder yet?" has no right answer, so it drifts per author and per project - which is how the repo arrived at eight names for one role.

`find src/features -maxdepth 1 -type f` MUST return nothing.

### 2.4 `sdk/`

`sdk/` mirrors the game's own hierarchy, not the mod's. Subdivide by the engine's domains (`entities/`, `streaming/`, `ui/`, `ue/`, `cry/`, `red/`) and keep `patterns.h` plus the `Patterns` struct at its root. One `SDK::` namespace root (with engine sub-namespaces), distinct from the mod namespace.

### 2.5 Known deviations to fix

The remaining migrations follow the same shape: for each feature, create `features/<feature>/` and move its hook file, its module, its bindings and its RPC headers into it.

| Project | Deviation | Target |
|---|---|---|
| remaining projects | features split across `core/hooks/`, `core/modules/`, `scripting/` or `core/builtins/`, `shared/rpc/` | one `features/<feature>/` folder per side |
| remaining projects | hand-maintained source lists in `CMakeLists.txt` | glob `src/features/*/*.cpp` |
| remaining projects | feature owners named `Module` / `Manager` / `Controller` / `Bridge` / `Mirror` / `Relay` / `Presenter` / `Worker` / `System` | `<Feature>Service`, see §7.1 |
| `KCD2MP` (11), `m2o` (2), `cybermp` (2) | feature owners reached through `static X &Get()` singletons | members of `Application` / server `Instance`, see §7.5 |
| `m2o` | feature owners loose at `core/` root (`season_manager.h`, `loading_screen_presenter.h`) | `features/season/`, `features/loading_screen/` |
| `cybermp`, `m3o` | RPC payloads inlined at call sites | `shared/features/<feature>/<payload>.h` |
| `vanta` | `core/nui/` parallel to `core/ui/`; `game/sync/` instead of `game/replicator/` | shell UI in `core/ui/`, capture in `game/replicator/` |

---

## 3. Naming

| Thing | Convention | Example |
|---|---|---|
| Files, directories | `snake_case` | `vehicle_lock_hooks.cpp`, `core/states/` |
| `sdk/` files | `snake_case` of the game symbol | `C_Player2` -> `sdk/entities/c_player2.h` |
| Namespaces | `PascalCase`, rooted at the project name | `M2O::Features::Jukebox`, `KCDMP::Game` |
| Feature folder | `features/<stem>/`, stem singular snake_case | `features/jukebox/` |
| Game-binding namespace | `SDK` plus engine sub-namespaces | `SDK::mafia::gui`, `SDK::ue::sys` |
| Types | `PascalCase`, `final` when not a base | `class WorldClock final` |
| Functions, methods | `PascalCase` | `RequestWorldUnload()` |
| Private members | `_camelCase` | `_worldLoadPending` |
| Public data on replicated entities | `camelCase`, no prefix | `nickname`, `modelIndex` |
| Compile-time constants | `k` + `PascalCase` | `kStreamRange`, `kNoTeam` |
| Enumerators (`enum class`) | `PascalCase` | `VehicleLockState::FullyLocked` |
| Enumerators (game-defined) | the game's own spelling | `ENTITY_TYPE_CAR` |
| File-scope globals | `g` + `PascalCase`, and see §4 | `gPatterns`, `gApplicationModule` |
| Feature entity | `Feature` in `features/<stem>/<stem>.h` | `Jukebox` |
| Feature service | `FeatureService` in `features/<stem>/<stem>_service.h` | `JukeboxService` |
| Script bindings | `Feature` in `<Project>::Scripting`, `features/<stem>/<stem>_scripting.h` | `Jukebox` |
| Hook files | `features/<stem>/<stem>_hooks.cpp` | `jukebox_hooks.cpp` |
| Hook trampolines | `<Symbol>_Hook` / `<Symbol>_Original` | `CommandInitFnDeath_Hook` |

Namespace blocks close with a comment: `} // namespace M2O::Core`.

---

## 4. File-local state and anonymous namespaces

Anonymous namespaces are correct C++ and are **required** in a `.cpp` for detour trampolines and pure helpers. The problem is not the namespace - it is what projects put inside it. Approximate counts of cross-call mutable file-locals inside anonymous namespaces in mod code today (grep-derived, excluding `sdk/` and `_Original` trampoline pointers): `hogwarts` ~242, `vanta` ~81, `m2o` ~60, `KCD2MP` ~22, `cybermp` ~5, `MafiaMP` ~1.

Every one of those is mod state that no owner declares, no lifecycle resets, no other translation unit can read, and nothing can test.

**MUST** - anonymous namespace, in a `.cpp` only:

- detour typedefs, `_Original` pointers, and `_Hook` thunks;
- pure helper functions, holding no state that survives the call;
- `const` / `constexpr` tables local to the file.

**MUST NOT** - anything in an anonymous namespace, or `static` at file scope, whose value must survive across calls, frames, sessions, or connections. That is *mod state*. It belongs to an owner: the `Application`, an entity module, or a service under `features/<stem>/` - something with a constructor, a reset path, and a header other code can reach.

**Reset discipline.** Every piece of mod state MUST be reachable from a lifecycle reset. Session state resets in `OnConnectionClosed()` or the disconnection state; world state resets on world unload. State hidden in a translation unit cannot participate, which is exactly how stale values survive a reconnect.

```cpp
// FORBIDDEN - survives reconnects, no owner, unreachable, untestable
namespace {
    bool g_worldReady             = false;
    uint32_t g_lastGeneration     = 0;
    std::vector<uint64_t> g_pendingSeats;
}

// CORRECT - owned, reset with the session
namespace M2O::Core::Modules {
    class WorldState final {
      public:
        void Reset();
        bool IsReady() const { return _ready; }
      private:
        bool _ready = false;
        uint32_t _generation = 0;
        std::vector<uint64_t> _pendingSeats;
    };
} // namespace M2O::Core::Modules
```

**Allowed exception.** A single process-wide owner published as `extern` in its header - `SDK::gPatterns`, `M2O::Core::gApplicationModule`. These are declared, reachable and documented; they are not hidden. Adding a new one requires a comment explaining why an owner cannot hold it.

`static` at file scope in a `.cpp` is equivalent to an anonymous namespace for these rules. Prefer the anonymous namespace so the intent is explicit; `static InitFunction init(...)` is the established exception (see §9).

---

## 5. Events come from hooks, not polling

The rule the projects most consistently break. Polling a game value every frame to notice a change means the mod learns about the event *late*, *sometimes never* (two changes inside one frame collapse), and *without context* - the hook had the actor, the weapon and the damage record; the poll has only the after-state.

### 5.1 The rule

**A local game event MUST be observed by hooking the game code that produces it.** Reading local game state on a timer to infer that something happened is forbidden when a hookable call site exists.

**Diffing replicated state on apply is not polling and is correct.** A remote entity's authoritative fields arrive as a snapshot; applying only what changed is the intended pattern. Distinguish by the *source of truth*:

| Source of truth | Correct mechanism |
|---|---|
| Local game (the player fired, entered a car, died, changed district) | hook the call site, dispatch through `LocalEvents` |
| Server / replicated entity (a remote's health, a scripted teleport, a lock state) | diff-on-apply in the module's `Frame()` / `OnDeserialized()` |
| Genuinely unhookable game state (no stable call site found) | poll, with a comment naming what was searched for and why it failed |

`m2o/code/client/src/core/modules/human.cpp:642` (`danger != _lastInjectedDanger`) is the second kind and is correct. `hogwarts/code/client/src/core/local_player_events.cpp:110` (`readinessMask != _lastReadinessMask`, recomputed on every `Refresh()`) is the first kind and should be edge-driven from the world-ready hook.

### 5.2 Hooks stay thin; the service owns policy

M2O states the contract in its own `local_events.h`:

> The hooks stay thin (detect an accepted local event and forward it here); this resolves network entities, raises the reserved client event, and is the single place that owns the matching net broadcast to the server when one is due.

That is right, and with features as folders the "here" is the feature's own service rather than one central class. Concretely:

- a hook file **MUST NOT** send an RPC, touch a replica, or call into scripting;
- a hook detects the event, extracts the raw arguments, and calls one `Notify*` method on **its own feature's service** - the file next to it;
- the service resolves network entities, emits the reserved script event, and performs the one broadcast;
- those methods are named for what happened: `EnteredVehicle`, `Submerged`, `PinupCollected`, `WaypointPlaced`.

`core/local_events.{h,cpp}` survives, narrowed to what only it can carry: shell-level events with no owning feature, and events that fan out to several features. A single `LocalEvents` that every hook in the mod calls does not scale - at a hundred features it is a thousand-line header that every feature has to edit, which is the coupling the folder layout exists to remove.

```cpp
// hook file - thin
char __fastcall CommandUpdateFnAnimPlay_Hook(void *human, void *edx, float dt, void *command) {
    const char result = CommandUpdateFnAnimPlay_Original(human, edx, dt, command);
    if (!result) { M2O::Core::Modules::Human::OnAnimCommandFinished(reinterpret_cast<SDK::C_Human2 *>(human), command); }
    return result;
}
```

`LocalEvents` methods carry a comment stating what raises them, whether they emit a script event, and whether they broadcast. This is the one file where dense comments are expected - it is the mod's event contract.

### 5.3 Server side

The server mirror is `features/<stem>/<stem>_handlers.cpp`: one `Register<Feature>Handlers(Instance &)` per feature, declared in the feature's service header. Every handler stamps the authoritative actor from the sending connection before relaying or scripting the event. Per-tick authoritative progression (clocks, epochs, decay) belongs in the feature's service `Update()`, not scattered across handlers.

---

## 6. Constants: `enum class`, `constexpr`, plain `enum`, `#define`

Current spread in mod code (headers, `sdk/` excluded): 86 `enum class`, 112 plain `enum`, 398 `static constexpr`, 2 `#define` constants. Four tools for one job, chosen ad hoc. The rule is decided by **who owns the value** and **how the value is used**.

| The value is | Use | Why |
|---|---|---|
| A closed set the **mod** defines and puts on the wire | `enum class X : <fixed width> {}` | Scoped, no implicit conversion, explicit wire width |
| A set the **game** defines (engine ids, vtable indices, message types) | plain `enum X : <fixed width> {}` in `SDK::`, enumerators spelled as the game spells them | It is a transcription; a scoped enum forces a cast at every native call |
| A **bit mask** | either a `struct X { static constexpr uint32_t A = 0x1; ... };` or an unscoped fixed-width `enum` **nested in the struct that owns the field** | Both give scope and width without operator boilerplate. A scoped `enum class` used as flags is forbidden - see below |
| A **tuning value, limit or rate** | `static constexpr` inside the type that owns it | Lives next to what it constrains |
| A **wire-protocol constant** used by both sides | `static constexpr` in the `shared/` header that defines the message | One definition, both sides |

**`enum class` MUST NOT be used for a bit mask.** It has no bitwise operators, so every combine and test becomes a cast, which is exactly what the scoping was supposed to prevent. An `enum class Flags : uint64_t` of `1ull << n` values forces a `static_cast` at every combine, test, and call site - the scoping buys nothing and the casts hide real mistakes. `HogwartsMP::Shared::Modules::HumanSync` gets this right: `enum StateFlag : uint16_t` nested inside the struct that owns the field - scoped by the enclosing type, fixed width, no casts.

**`#define` for a constant is forbidden.** It has no type, no scope, no debugger visibility, and collides with the game's own headers. The only permitted `#define`s are compilation switches (`FW_PROFILING`, `FW_NODE_INSPECTOR`) and toolchain workarounds that must be textual, such as `THISCALL` in `hooks_common.h`. Only 2 offenders remain in mod code, so this is a cheap sweep - the `#define`-heavy files under `hogwarts/code/client/src/sdk/ue/` are imported engine sources and out of scope.

**A fixed underlying type is mandatory** on any enum that is serialized, stored in a replicated field, or passed to native code: `enum class VehicleLockState : uint8_t`, never a bare `enum class`. Current offenders on the wire:

```cpp
// m2o/code/shared/game/season.h - replicated through the session config, no width
enum class Season { Summer, Winter };                       // -> enum class Season : uint8_t

// MafiaMP/code/shared/modules/mod.hpp - unscoped, no width, SCREAMING enumerators
enum EntityTypes { MOD_PLAYER, MOD_VEHICLE };               // -> enum class EntityKind : uint8_t { Player, Vehicle }

// hogwarts/code/shared/game/weather.h - mod-defined closed set written as a game transcription
enum SeasonKind : uint8_t { SEASON_SPRING, SEASON_SUMMER }; // -> enum class SeasonKind : uint8_t { Spring, Summer }
```

```cpp
// mod-defined, on the wire
enum class VehicleLockState : uint8_t { Unlocked = 0, Breakable = 1, FullyLocked = 2 };
constexpr uint8_t kVehicleLockStateMax = static_cast<uint8_t>(VehicleLockState::FullyLocked);

// game-defined transcription
enum E_EntityType : uint32_t { ENTITY_TYPE_HUMAN2 = 14, ENTITY_TYPE_CAR = 18 };

// bit mask
struct ControlStyle {
    static constexpr uint32_t Walk = 0x00000001;
    static constexpr uint32_t Fire = 0x00000020;
    static constexpr uint32_t All  = 0x00FE7FFF;
    static constexpr uint32_t NoWeapons = All & ~(Fire | WeaponManip | WeaponSelect);
};

// tuning owned by the entity
class HumanEntity : public Replication::NetworkEntity {
    static constexpr float kStreamRange       = 250.0f;
    static constexpr uint32_t kInterestBudget = 60;
};
```

**Derive, don't restate.** `NoWeapons` is written as `All & ~(...)`, not as a second literal. Any constant that is a function of another MUST be written as that expression.

**Lifecycle state ids** are the documented exception to `enum class`: `states.h` declares `enum StateIds : int32_t {}` unscoped, because `Framework::Utils::States::IState::GetId()` returns `int32_t` and every state would otherwise cast.

---

## 7. Feature modules

The same role is named eight different ways across the projects, and one name means three different things. `Manager` is currently all of:

- `vanta/.../server/src/game/player_manager.h` - server-side lifecycle owner for a replicated entity kind (add, remove, spawn);
- `m2o/.../client/src/core/season_manager.h` - client-side feature owner with `Update()` and `Reset()`;
- `hogwarts/.../server/src/core/builtins/beast_manager.h` - a v8 binding class in `HogwartsMP::Scripting` whose only method is `Register(isolate, global)`.

Alongside it: `Module` (KCD2MP), `Service` (m2o, hogwarts), `Controller` (KCD2MP, cybermp, MafiaMP), `Bridge` and `Mirror` (hogwarts, cybermp), `Relay`, `Presenter`, `Worker`, `System` - and features with no owning class at all, whose state sits in a hook file's anonymous namespace (§4).

A feature is one folder per side (§2.3) containing every file that implements it. Inside it, each file has one of five roles, and each role has one name and one mechanical test.

### 7.1 The five roles

| Role | File | Class | Instances | Defining test |
|---|---|---|---|---|
| **Entity** | `<feature>.{h,cpp}` | `Feature` | one per replicated instance | derives from the feature's `_entity.h` type |
| **Service** | `<feature>_service.{h,cpp}` | `FeatureService` | exactly one, owned by `Application` / server `Instance` | has `Update()` and `Reset()`; owns state that is not on a replica |
| **Bindings** | `<feature>_scripting.{h,cpp}` | `Feature` in `<Project>::Scripting` | none - static registration | its only non-callback member is the registration entry point |
| **Hooks** | `<feature>_hooks.cpp` | none | none | free functions plus one `InitFunction` |
| **Native override** | `game/overrides/<name>.{h,cpp}` | the game's own name | as the game creates them | derives from an `SDK::` type |

**Retired suffixes.** `Module`, `Manager`, `Bridge`, `Mirror`, `Relay`, `Presenter`, `System`, and `Worker` are replaced by `Service`. `Controller` and `Worker` survive **only** on native overrides, where the name mirrors the game's own type - `MafiaMP::CharacterController` (`: SDK::ue::game::humanai::C_CharacterController`), `CyberMP::RemotePlayerController` (`: SDK::game::Player`), `M2O::ServerWorldLoadWorker` (`: C_LoadingScreenWorker`) keep their names for that reason. `Handler` survives only as the free functions in `<feature>_handlers.cpp`.

### 7.2 One folder, one feature, one grep

The point of the layout is that a feature has an address, and it has one from the day it is created - see §2.3, the folder is not conditional on size. `ls features/jukebox/` is the whole client-side feature; `git log -- 'code/*/src/features/jukebox/'` is its history; a feature is deleted by deleting three directories and one line from `register_entities.cpp`.

The stem is the feature's name in the domain, singular, snake_case, and identical on every side - no `vehicle` here and `car` there.

### 7.3 Cross-feature dependencies

At a hundred features this is what decides whether the layout survives.

- A feature MAY include another feature's **entity header** and **service header**. It MUST NOT include another feature's `_hooks.cpp`, `_scripting.*`, or any private header.
- Cross-feature calls go **service to service**. A hook or a binding reaching into another feature is a violation; it goes through its own service, which calls the other's.
- **Cycles are forbidden.** If two features need each other, either one owns the relationship, or the shared part moves down into `game/` or `shared/`.
- Behaviour that three or more features need is no longer feature-specific: move it to `game/` (if it speaks the game's terms) or `core/` (if it is shell policy).

### 7.4 Service shape

KCD2MP's `DoorModule` is the best existing feature owner in the repo and its interface is the model - it is the ownership and the name that change, not the shape:

```cpp
namespace KCDMP::Features::Door {
    class DoorService final {
      public:
        static void Install();                                  // register replica type + RPC slots; once, at startup
        void Update();                                          // per frame
        void Reset();                                           // session ended: drop everything
        void NativeContextChanged();                            // the native world this was bound to is gone

        void NotifyLockChanged(SDK::EntityId door, bool locked); // local edges, from this feature's hooks (§5.2)
        void NotifyLockpicked(SDK::EntityId door);
    };
} // namespace KCDMP::Features::Door
```

- `Install()` is static and idempotent, and is the only place the feature's replica types, RPC slots and script bindings are registered.
- `Update()` and `Reset()` are the only two methods the shell calls. Every service MUST have both, even when one is empty - that is what makes them a list.
- `Notify*` methods are the entry points its own hooks call (§5.2). The service resolves entities, raises the script event, and owns the broadcast.

### 7.5 Services are owned, not singletons

A service is a member of the `Application` (client) or the server `Instance` subclass, reached through an accessor - the way M2O already owns `_localEvents`, `_debugUI` and `_toastUI`. `static Service &Get()` retires: KCD2MP has 11 such singletons, M2O 2, cybermp 2.

The reason is §4's reset discipline. A singleton's lifetime is the process, so a service that forgets `Reset()` leaks session state across a reconnect and nothing in the code says so. Members are constructed in declaration order and swept by one loop in `PostUpdate()` and one in `OnConnectionClosed()` - a new service cannot silently fail to join.

At a hundred features the shell cannot name every service as a member by hand. Hold them in one owned container built at startup - a `std::vector<std::unique_ptr<IFeatureService>>` plus a typed accessor - so `Update()` and `Reset()` stay single loops and registration is one line per feature in one file. That file is the only place the shell knows features exist.

Hooks do not need a singleton to reach their service: `Install()` runs at startup and the hook file is in the same folder as the service that owns it. Where a hook genuinely runs before the `Application` exists - pattern installation, crash handlers - it uses the declared `extern` owner of §4, not a new singleton.

### 7.6 Scripting lives with its feature

A feature's bindings are `<feature>_scripting.{h,cpp}` inside the feature folder, on both sides. A feature is not readable if the JS surface it exists to provide lives somewhere else, and at this scale a shared `scripting/` folder is just a second copy of the feature list, maintained by hand, drifting.

What kept the old layer-folder honest has to be kept by rule instead, and both rules are greps:

- **A `*_scripting.{h,cpp}` MUST NOT name an `SDK::` type, a replica, or a network peer.** It validates arguments and calls its own service's public API. A binding that needs something the service does not expose is a missing service method, not a reason to reach past it.
- **Bindings register themselves from the feature's `Install()`**, and their catalog metadata alongside. `shared/scripting_catalog.h` keeps only the catalog handles (`ClientCatalog()`, `ServerCatalog()`) and the common event metadata; the per-feature entries move to the feature. The registration call list lives in `core/scripting_registry.cpp` - one line per feature, the same file that registers services.

The one thing the layer folder did that a rule cannot replace is enumeration, and the folder-per-feature glob replaces it: the binding set is `find src/features -name '*_scripting.cpp'`.

### 7.7 There is no "no owner" option

If a feature has state that survives a call, it has a service. Free functions in a hook file with their state in an anonymous namespace is not a lighter-weight alternative - it is the §4 violation and the §5.2 fat-hook violation at the same time, and it is why those features have no reset path today. A feature with no cross-call state at all needs no service: a hook that forwards straight into another feature's service is complete on its own.

---

## 8. Entities and replication

- A replicated type is defined **once**, in `shared/features/<stem>/<stem>_entity.h`, deriving from `Framework::Networking::Replication::NetworkEntity`.
- It declares `static constexpr const char *kTypeName = "<Project>::<Name>";` and its streaming and quantization constants as `static constexpr` members.
- All types register in one place: `shared/register_entities.cpp`, called once per side before connecting. Framework-provided entities (`TextLabelEntity`) go in the same list.
- Serialization splits three ways and the split MUST be respected: `OnSerializeConstruction` (spawn metadata), `SerializeTransform` (high-frequency, unreliable), `SerializeFields` (low-frequency reliable deltas). A rarely-changing string in `SerializeTransform` costs bandwidth on every idle player.
- Quantize explicitly - `FloatBits`, `Float16` with stated ranges - and comment the resulting precision.
- Server-only bookkeeping fields live on the same class, are not serialized, and are marked `// server-only, not serialized`.
- The **client half** is `features/<stem>/<stem>.{h,cpp}`, a `final` subclass of the shared entity that owns the native game object. It implements the Replica3 hooks (`OnConstructed`, `OnDeserialized`, `OnStateForced`, `DeallocReplica`) and a `Frame()`.
- The owner/remote asymmetry is stated in the class comment: the owner captures native state upstream; remotes inject received state and apply only authoritative outcomes.

---

## 9. Hooks and patterns

- Pattern addresses live in one `SDK::Patterns` struct resolved in `InitPatterns()`; a project ships a prebuilt table at `client/data/<project>.patterns` loaded via `hook::load_pattern_table`.
- Per **AGENTS.md rule 1**, a resolved `gPatterns.*` entry MUST NOT be null-checked. Call `MH_CreateHook` / `hook::put` on it directly - no staging local, no "was not resolved" warning.
- Hook installation uses `static InitFunction init([]{ ... }, "<Name>");` at the bottom of the feature's `<stem>_hooks.cpp`. The name string is mandatory and matches the feature.
- Byte patches (`hook::put`) carry a comment giving the target symbol, the offset and the intended instruction - see M2O's `vehicle_lock_hooks.cpp`.
- Non-trivial hook groups reference their research note: `// See docs/research/<topic>.md`.

---

## 10. Scripting

- Bindings live with their feature: `features/<stem>/<stem>_scripting.{h,cpp}`, one JS namespace per feature, on both client and server (§7.6).
- A binding translates; it does not implement. It MUST NOT name an `SDK::` type, a replica, or a network peer - it validates arguments and calls its own service. Control flow beyond that is a missing service method.
- Each binding registers its own catalog metadata - including the exact argument tuple of any event it can raise - through `ClientCatalog()` / `ServerCatalog()` from `shared/scripting_catalog.h`, which keeps only the catalog handles and the common event metadata. The catalog generates the TypeScript contract; an unregistered binding is invisible to consumers.
- Reserved event names are documented at the `Notify*` method that raises them (§5.2).
- Registration is one line per feature in `core/scripting_registry.cpp`, reached from `Application::ModuleRegister(Framework::Scripting::Engine *)` - a call list, not scattered static initializers.

---

## 11. Lifecycle

- The client `Application` derives from `Framework::Integrations::Client::Instance` and overrides only what it needs: `PostInit`, `PostUpdate`, `PreShutdown`, `ModuleRegister`, `OnConnectionFinalized`, `OnConnectionClosed`, `OnConnectionPhaseChanged`, `OnInitialAssetDownloadReady`, `OnProtocolLaunch`.
- Session and world progression is a `Framework::Utils::States::Machine` owned by the `Application`, one `IState` per file under `core/states/`, ids in `states.h`. The canonical set: `Initialize -> GameSetup -> MainMenu -> SessionConnection -> SessionConnected -> SessionDisconnection -> Shutdown`, plus an offline/debug state.
- A state's `OnUpdate` drives *transitions*. It MUST NOT accumulate gameplay state; that belongs to a service the state starts and stops.
- Game-engine tick integration (the game's own module interface) is a separate `ApplicationModule` under `core/`, forwarding engine callbacks and owning world load/unload bookkeeping. Keep it distinct from `Application`: one talks to the game loop, the other to the framework.

---

## 12. Comments and formatting

- `.clang-format` is authoritative: LLVM base, `IndentWidth: 4`, `ColumnLimit: 280`, `NamespaceIndentation: All`, `PointerAlignment: Right`, `AlignConsecutiveAssignments: Consecutive`. Run `scripts/format_codebase.sh`.
- Keep statements and comments on one line; the 280-column limit exists for that.
- Comments explain **why**, never what. A comment restating the code is deleted, not reworded.
- Where a comment is expected: a class header stating ownership and the owner/remote split; a `LocalEvents` method stating what raises it and what it broadcasts; a byte patch stating the target instruction; a quantization constant stating the resulting precision; any documented exception to a `SHOULD` in this file.
- `#pragma once`, never include guards.

---

## 13. Revamp checklist

Use this to bring a project to the reference, or to review a change against it. Every item is mechanically checkable.

**Structure**

1. Every feature is one folder per side: `src/features/<stem>/`, whatever its size - `find src/features -maxdepth 1 -type f` returns nothing. No `core/hooks/`, `core/modules/`, `core/builtins/` or top-level `scripting/` bucket remains; the only hook files outside `features/` are the shell's own in `core/boot/`.
2. All file and directory names are `snake_case`; every file in `features/<stem>/` starts with `<stem>_` (or is `<stem>.{h,cpp}`).
3. `CMakeLists.txt` globs `src/features/*/*.cpp`; no feature source is hand-listed.
4. No file in `sdk/` includes framework or networking headers; no file in `shared/` includes `sdk/`. No committed build output under the project.

**State**

5. `grep -rn 'namespace {' --include=*.h` returns nothing - anonymous namespaces are `.cpp`-only.
6. No mutable variable survives a call inside an anonymous namespace or at `static` file scope, except `_Original` trampoline pointers and `const`/`constexpr` tables.
7. Every `extern` global is declared in a header with a comment justifying it.
8. Every piece of session state is reset from `OnConnectionClosed()` or the disconnection state.

**Events**

9. `core/local_events.{h,cpp}` carries only shell-level and cross-feature events; a feature's own local events go to its service's `Notify*` methods.
10. Every `*_hooks.cpp` forwards and returns - no RPC send, no replica access, no scripting call, no cross-call state.
11. Every `!= _last` / `!= g_last` comparison either applies replicated state or carries a comment naming the call site that was searched for and not found.
12. Server RPC handlers are one `Register<Feature>Handlers(Instance &)` per feature in `features/<stem>/<stem>_handlers.cpp`, declared in that feature's service header.

**Feature modules**

13. Every feature owner is a `<Feature>Service` in `features/<stem>/<stem>_service.{h,cpp}` with both `Update()` and `Reset()`. No `Module`, `Manager`, `Bridge`, `Mirror`, `Relay`, `Presenter`, `System` or `Worker` suffix survives outside `game/overrides/`.
14. `grep -rnE 'static [A-Za-z0-9_:]+ *&? *(Get|Instance|GetInstance) *\(\)'` over mod headers returns nothing - services are members of `Application` / server `Instance`.
15. Every service is reached by exactly one `Update()` call site and one `Reset()` call site, both sweeping the shell's service container.
16. No `*_hooks.cpp` sends an RPC, touches a replica, or calls scripting; it calls a `Notify*` on a service.
17. No `*_scripting.{h,cpp}` names an `SDK::` type, a replica, or a network peer.
18. No feature includes another feature's `_hooks.cpp`, `_scripting.*`, or a private header; cross-feature calls are service to service, and there are no include cycles.
19. `core/` holds only the shell - `application`, `application_module`, `local_events`, `crash_reporting`, `states/`, `scripting_registry`, and shell-hosting UI. Anything in `game/` is used by two or more features.

**Constants**

20. `grep -rnE '^#define [A-Z_]+ [0-9x]' code/projects/<p>/code --include=*.h --include=*.cpp`, with `sdk/` excluded, returns nothing outside compilation switches.
21. Every enum that is serialized, stored in a replicated field, or passed to native code has an explicit underlying type.
22. Mod-defined closed sets are `enum class`; game transcriptions are plain `enum` in `SDK::`; bit masks are `struct` + `static constexpr`, or an unscoped fixed-width `enum` nested in the owning struct - never `enum class`.
23. No constant restates a value derivable from another.

**Replication**

24. Every replicated type is declared once under `shared/features/<stem>/` and registered in `shared/register_entities.cpp`.
25. `SerializeTransform` carries only high-frequency data; strings and rarely-changing fields are in `SerializeFields`.
26. Every quantized field states its range and resulting precision.

**Hooks**

27. No `gPatterns.*` entry is null-checked or staged through a tested local.
28. Every `InitFunction` has a name string; every byte patch has a target/offset/instruction comment.

**Scripting**

29. Every binding registers its own catalog metadata with its exact event tuples, and is reached from the one call list in `core/scripting_registry.cpp`.
30. No binding contains control flow beyond argument validation and a single call.

---

## 14. Using this document with an AI agent

For a global revamp, drive one section at a time and one project at a time. The prompt shape that works:

> Read `docs/project_architecture.md`. Apply **§4 (file-local state)** to `code/projects/<project>/code/client/src/features/`. For each mutable file-local that survives a call, move it into that feature's `<stem>_service` as a private member, add the reset path, and update call sites. Do not change behaviour. Do not touch `sdk/`. Build with `builds\build.bat <Target> 64`.

Rules for the agent:

- One section per pass, in this order. The sections interact, and this sequence is the one where each pass has somewhere to put its output:
  1. **§2, §3** - carve `features/<stem>/` and move each feature's hooks, module, bindings and RPC headers into it; switch CMake to globs. Pure moves and renames, reviewable as a diff of paths, no behaviour change.
  2. **§7** - give every feature a `<Feature>Service` with `Update()`/`Reset()`, retire the singletons, register through the shell's one call list. Mostly empty shells at this point.
  3. **§4** - move file-local state into the services §7 just created. Doing §4 first has nowhere to put the state and invents ad-hoc owners.
  4. **§5** - replace source-side polls with hooks. Moving the state in §4 is what makes the polls visible.
  5. **§6**, then **§8**-**§11** - constants, replication, scripting, lifecycle.
- Behaviour-preserving refactors and behaviour changes go in separate commits.
- Commits follow `Module: Brief description`, wrapped at 72 characters, atomic, rebased on `develop`.
- Version impact per `AGENTS.md`: touching `shared/` entity or RPC definitions is MAJOR; touching `scripting/` is MINOR; everything else here is PATCH.
- When a rule cannot be applied, leave the code alone and report why. Do not invent a half-measure.
