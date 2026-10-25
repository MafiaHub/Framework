# Prebuilt Framework: mods link a released SDK instead of building in-tree

Research document. No code changes accompany it. It records how a mod is built
against the Framework today, what couples the two trees, the precedents this
repository already has for consuming prebuilt dependencies, the delivery options
for a prebuilt Framework, the package design that follows, and a staged plan to
get there without breaking the mods in flight.

Status of each statement is marked. `[verified]` means read from this repository,
one of the mod checkouts under `code/projects/`, or the cited upstream repository;
`[measured]` means a number taken from a build tree or a CI run on the stated date;
`[reported]` means general toolchain knowledge worth re-checking before it drives
a decision.

## 1. Summary

- **What changes.** Today a mod is a git repository cloned into
  `code/projects/<mod>/` and picked up by a glob; it compiles against Framework
  sources and inherits the Framework's flags, toolchain, vendors and output layout
  from the parent CMake tree. After the transition the Framework publishes a
  per-platform install tree (headers, static libraries, runtime files, CMake
  package, helper modules) on every version tag, and each mod is a top-level CMake
  project that pins a Framework version, downloads that archive at configure time
  and calls `find_package(Framework CONFIG)`. The `code/projects` glob goes away.
- **Recommended delivery.** Option A in section 5: a `cmake --install` tree
  attached to the GitHub release that `pr_tag_commit.yml` already creates for every
  version bump, consumed through a small `FrameworkBootstrap.cmake` that mirrors
  the existing MafiaNet pin (version + per-archive SHA-256,
  `file(DOWNLOAD EXPECTED_HASH)`). It is the same shape as the MafiaNet and
  MafiaHubServices precedents already in this tree, so nothing new has to be
  learned to operate it.
- **Keep static linking.** The Framework stays three static libraries plus the
  loader; the archive bundles every transitive non-vcpkg dependency it was built
  with and re-resolves the vcpkg ports through the same manifest baseline. A shared
  library SDK (option C) is the right end state for ABI isolation but needs export
  annotations across ~250 headers and has three known cross-DLL hazards (ImGui
  context, v8pp templates, CoreModules singletons); it is a later phase, not the
  first.
- **The one semantic change.** `FW_RPC_IDENTIFIER_SALT` is a compile-time constant
  baked into both Framework and mod translation units. A prebuilt Framework cannot
  carry a per-mod, per-release salt, so the salt has to become a runtime value
  (section 7.3). That is a MAJOR Framework change and it gates everything else.
- **What it buys and what it does not.** Reproducible mod releases (the SDK a
  release contains is a hashed artifact, not a commit of a tree that still fetches
  five things at configure time), one toolchain policy instead of five copies of
  it, and a mod configure that no longer bootstraps vcpkg (about four minutes
  cold). It does **not** fix the long release builds: the Framework is ~25% of
  HogwartsMP's 53-minute client job and the rest is the mod's own code behind a
  cold sccache (section 4.3).
- **Order.** Runtime salt and install/export rules in the Framework (both testable
  in-tree) -> release workflow attaches archives -> `FrameworkBootstrap.cmake` and
  the first mod (M2O, the baseline per `docs/project_architecture.md`) moves ->
  remaining mods -> delete the glob and the in-tree docs. Open decisions are in
  section 10.

## 2. How a mod is built today `[verified]`

### 2.1 Topology

- `code/CMakeLists.txt` globs `projects/*` and `add_subdirectory()`s every
  directory without an `IGNORE` file. Each mod is its own git repository cloned
  there (`code/projects/m2o` -> `mafia2online/Mod`, and so on for KCD2MP,
  MafiaMP, cybermp, fmp, hogwarts, m3o, tmp).
- Each mod already starts with `cmake_minimum_required()` and `project()`, so it
  is syntactically a top-level project, but it depends on being a subdirectory in
  the ways listed in 2.2.
- Release CI in every mod checks out `MafiaHub/Framework` at the SHA in the mod's
  `FRAMEWORK_PIN`, checks the mod out into `code/projects/<mod>`, configures the
  **Framework** tree with `-DFW_RPC_IDENTIFIER_SALT=<per-tag value>` and builds the
  mod's targets from inside it. PR CI in the mods no longer compiles at all
  (format and type-contract checks only), by explicit decision recorded at the top
  of `code/projects/hogwarts/.github/workflows/build-test.yml`.
- `builds\build.bat` is the only supported local entry point; it configures
  `builds\build-64` (or `build`, `build-prod-64`, `build-release-64`) for the
  whole tree, mods included.

### 2.2 Coupling inventory

Everything a mod takes from the parent tree without declaring it. Each row is a
thing the SDK package has to provide or the mod has to own after the move.

| Coupling | Where | SDK answer |
|---|---|---|
| Framework headers via `include_directories(${CMAKE_SOURCE_DIR}/code/framework ...)` and `PUBLIC $<BUILD_INTERFACE:...>` on the targets | every `code/projects/*/code/CMakeLists.txt`; `code/framework/CMakeLists.txt` `link_shared_deps` | `install(DIRECTORY src/ ...)` + `$<INSTALL_INTERFACE:include>` |
| Library targets `Framework`, `FrameworkClient`, `FrameworkServer`, `FrameworkLoader`, `FrameworkLoaderData`, `MafiaHubServices` | mod `target_link_libraries` | exported as `Framework::*` |
| Vendor targets used directly by mods: `imgui::imgui`, `Microsoft::DirectXTK`, `libnode`, `CEFCopyDlls`, `libnode_copy_dll`, `lua54_static` (MafiaMP vendors), `M2O::PhysX283Headers`, `expat` | mod `CMakeLists` | vcpkg ones re-found by `find_dependency`; copy targets replaced by a staging function |
| `CMAKE_MODULE_PATH` = Framework `cmake/`: `UpdateGitHash`, `FrameworkBinaryIdentity` (`fw_set_binary_identity`, `templates/binary_identity.rc.in`), `GetGitRevisionDescription` | every mod root `CMakeLists` | installed under `lib/cmake/Framework/modules/` |
| Flags inherited from `cmake/FrameworkSetup.cmake`: `/MD` in every config, `CMAKE_CXX_FLAGS_DEBUG "/Zi /external:W0" -DFW_DEBUG`, `/SAFESEH:NO`, `/bigobj`, `/Zc:__cplusplus`, warning set, output dir `${CMAKE_BINARY_DIR}/bin` | root `CMakeLists.txt`, `FrameworkSetup.cmake` | installed `FrameworkSetup.cmake` the mod includes after `project()` |
| vcpkg toolchain and triplets, `cmake/VcpkgBootstrap.cmake`, `vcpkg.json` | root `CMakeLists.txt` | mod carries its own manifest that **depends on** the SDK's dependency list, or the SDK bundles `vcpkg_installed` (section 6.4) |
| `FW_LAUNCHER_GAME_RESERVE` compiled into `src/launcher/game_reserve.cpp`, attached as `INTERFACE` source of `FrameworkLoader` | `code/framework/CMakeLists.txt`, KCD2MP/fmp/hogwarts launcher CMake | ship the `.cpp` and keep it an `$<INSTALL_INTERFACE:...>` source |
| Generated `launcher.manifest` and `/MANIFESTINPUT` on `FrameworkLoader` | same | ship the generated manifest in the archive |
| `/DEF:${LIBNODE_NODE_API_DEF}` on `FrameworkServer`'s interface link options (Node-API re-exports) | `cmake/NodeApiExports.cmake` + `code/framework/CMakeLists.txt` | ship the `.def`; keep the interface link option |
| `scripts/check_pattern_table.py` run as a build step by M2O, hogwarts, KCD2MP | mod client CMake | installed under `share/framework/scripts/` |
| `code/tests/unit.h` included by hogwarts and KCD2MP tests | mod tests CMake | installed under `include/framework-tests/` |
| Runtime files copied into `bin/`: CEF DLLs, `.pak`, `locales/`, `icudtl.dat`, `cef_subprocess.exe`, `epic_sign_in.exe`, `libnode.dll`/`libnode.so.*`, `crashpad_handler`, `FrameworkLoaderData.dll`, `MafiaHubUpdater.exe`, Discord and Steam DLLs | `vendors/cef`, `vendors/libnode`, `vendors/CMakeLists.txt`, framework CMake | installed under `bin/` + `fw_stage_runtime(<target>)` helper |
| `FW_DEBUG` used by 11 mod source files, defined by `FrameworkSetup.cmake`'s debug flags | mod code | stays in the installed `FrameworkSetup.cmake` |
| `${CMAKE_SOURCE_DIR}` assumed to be the Framework root (M2O, hogwarts, KCD2MP, tmp) | mod CMake | becomes the mod root; paths rewritten against the SDK prefix |

### 2.3 Build-time knobs that are compile-time today

| Knob | Set by | Effect on headers | Prebuilt consequence |
|---|---|---|---|
| `FW_RPC_IDENTIFIER_SALT` | release CI, `-D` per tag | `constexpr kIdentifierSalt` in `networking/rpc/rpc_identifier.h`; every RPC id is a constexpr SipHash of it, computed in Framework TUs (`replication_manager.cpp`, `rpc/*.h`) and mod TUs alike | must become runtime (7.3) |
| `FW_NODE_INSPECTOR` | `ON` in Debug, option otherwise; `PRIVATE` on `FrameworkNodeEngine`/`FrameworkServer` | none on layout (`_interruptDrainFn` is unconditional) | a per-variant property of the archive (Debug variant on) |
| `FW_PROFILING` -> `TRACY_ENABLE`, `TRACY_*` | option, default ON, on-demand | `utils/profiler.h` macros and Tracy inline zones | export `TRACY_*` as interface definitions; one variant |
| `FW_DLL_INJECTION` | option, default OFF, `PUBLIC` on `FrameworkLoader` | **changes layout**: adds an enumerator and members in `launcher/project.h`, `rgl_bypass.h` | SDK ships OFF only; a mod wanting it builds from source |
| `FW_IMGUI_DX12` | always on 64-bit | `PRIVATE` on `FrameworkClient` | fixed per architecture |
| `LIBNODE_V8_ONLY` | default ON for 32-bit Windows | selects `v8` monolith vs `libnode`; `FrameworkServer` absent | the x86 archive is client-only by construction |
| `MAFIAHUB_SERVICES_BUILD_FROM_SOURCE` | the services CI only | none | SDK always bundles the prebuilt adapter |
| `NOMINMAX`, `_USE_MATH_DEFINES`, `MG_ENABLE_LOG=0`, `NODE_WANT_INTERNALS=1` (tests, node engine) | per-target | some are needed by consumers of the same headers | export as `INTERFACE_COMPILE_DEFINITIONS` where the header needs them |

### 2.4 Header surface

- 248 headers and 157 translation units under `code/framework/src/`
  `[measured 2026-10-24]`.
- 111 distinct Framework headers are included across M2O, hogwarts, KCD2MP, fmp
  and MafiaMP sources; M2O alone uses 59, hogwarts 60 `[measured]`. Roughly half
  the tree is API in practice; the rest is internal but nothing marks the line.
- Third-party headers reached **through** Framework headers (counts are headers,
  not includes): v8 and v8pp 32 (all of `scripting/`), function2 34, glm 20, CEF
  14 (`gui/`), nlohmann 7, imgui 6, D3D 9/11/12 headers 13, spdlog 5, ftl 4,
  `node.h` 2, Tracy 1, Steam 1, Discord 1, cxxopts 1, MafiaNet headers in
  `networking/network_peer.h` `[measured]`. Every one of those header sets has to
  be on the consumer's include path, so the package must either bundle them or
  find them.

## 3. Precedents already in this tree `[verified]`

The repository already consumes four prebuilt dependencies. The SDK should look
like them, and two of them are the direct template.

- **MafiaNet** (`vendors/CMakeLists.txt`, `cmake/MafiaNetPin.cmake`). Its release
  workflow runs `cmake --install` per platform and attaches
  `MafiaNet-<ver>-<platform>.{zip,tar.gz}` to the tag, each a full install tree
  with `MafiaNetConfig.cmake`/`MafiaNetTargets.cmake`, static libraries and the
  bundled Opus/RNNoise archives. The Framework pins version plus five SHA-256
  values in one file, downloads with `EXPECTED_HASH`, `find_package(... CONFIG
  PATHS ... NO_DEFAULT_PATH)`, then maps every consumer configuration onto the
  single Release archive with `MAP_IMPORTED_CONFIG_*` and marks the targets
  `IMPORTED_GLOBAL`. OpenSSL is resolved by `find_dependency`, not bundled. This
  is the whole recipe for option A.
- **MafiaHubServices** (`code/framework/src/services/CMakeLists.txt`,
  `.github/workflows/update_services.yml`). Prebuilt by a matrix workflow from a
  private repository, published as assets on a rolling `MafiaHub-Services`
  release with a `build.txt` timestamp, and consumed as an `IMPORTED` archive
  with a `BUILD_FROM_SOURCE` escape hatch. Two lessons: the dual source/prebuilt
  mode is exactly the developer override the SDK needs (section 8.3), and a
  rolling timestamp is **not** a pin - the SDK must version, not roll.
- **libnode / V8** (`vendors/libnode/CMakeLists.txt`) and **CEF**
  (`vendors/cef`, `cmake/DownloadCEF.cmake`). Version-pinned downloads at
  configure time with `IMPORTED` targets and copy targets for runtime files; CEF
  supports a shared cache directory (`FW_CEF_CACHE_DIR`). The SDK bootstrap
  should accept the same kind of cache variable so worktrees share one extract.
- **`FRAMEWORK_PIN`** in each mod. Already the record of "which Framework this
  release contains", already parsed by the release workflows, already overridable
  per run via the `framework_ref` dispatch input. Its content changes from a
  commit SHA to a version plus archive hashes; its role does not.
- **Release plumbing in the Framework.** `pr_tag_commit.yml` runs
  `.github/bump_version.sh` on every merge to `develop`, commits `VERSION`, tags
  `v<version>` and creates a GitHub release with generated notes (latest remote
  tag `v37.0.4`). 61 bumps in the 92 days to 2026-10-24, about five a week
  `[measured]`. The release exists; it has no binary assets. Attaching archives is
  an added job, not a new pipeline.

## 4. Goals, non-goals, and what the numbers say

### 4.1 Goals

1. A mod repository is a complete, standalone CMake project. Cloning it and
   running its own build script produces a client and a server.
2. A tagged mod release names a Framework **version** whose bits are a hashed
   artifact; rebuilding the tag reproduces the inputs without checking out a
   second repository.
3. The Framework repository builds and tests itself and produces the SDK; it does
   not know which mods exist.
4. Framework developers keep a fast loop: a mod can be pointed at a local
   Framework checkout and rebuild it incrementally (8.3).

### 4.2 Non-goals for the first delivery

- A stable binary interface across Framework versions. Mods rebuild against each
  SDK they adopt, as they do today. ABI stability is what option C would buy and
  is explicitly deferred.
- Curating a public header subset. Phase 1 installs every header; the line is
  drawn later with data (2.4).
- Changing the MafiaNet, libnode or CEF delivery. They stay as they are and are
  bundled into the SDK as-is.

### 4.3 Build time, so it is not oversold `[measured 2026-09-21]`

From hogwarts-mp release run 35616638455 (Windows client job, 51.2-minute build
step, 1012 ninja edges):

| Share of the client build | Minutes | Edges |
|---|---|---|
| Mod's own translation units | 37.5 | 509 |
| Framework + FrameworkClient + FrameworkLoader | 9.5 | 134 |
| CEF wrapper | 2.2 | |
| Other vendors | 1.6 | |
| Configure | 0.7 | |

A prebuilt Framework removes at most the middle three rows, about 13 of 53
minutes. The same job built in 391 seconds on 2026-09-10 with a warm sccache; the
decay since is cache coldness, not Framework compile time. The SDK is a
reproducibility and decoupling change. Say so in the PR descriptions, or the
first release that is still slow will be read as the SDK failing.

What the SDK does remove from every mod configure: the vcpkg fetch and bootstrap
(about four minutes cold, per the hogwarts `FRAMEWORK_PIN` note), the CEF
wrapper compile, and the MafiaNet/libnode/CEF downloads if the archive bundles
them (6.4).

## 5. Delivery options

### Option A - install tree on the GitHub release (recommended)

The MafiaNet model. `install(TARGETS ... EXPORT FrameworkTargets)`,
`install(EXPORT ...)`, a `FrameworkConfig.cmake.in` with `find_dependency` for the
vcpkg ports, plus `install(DIRECTORY)` for headers, runtime files, helper modules,
scripts and `unit.h`. A release job builds each platform, runs `cmake --install`,
archives the prefix, prints SHA-256s and attaches the archives to the tag.

- Pros: no new infrastructure; identical to how this tree already consumes
  MafiaNet; the archive is immutable and hash-pinned; works offline once cached;
  trivially inspectable (unzip and look).
- Cons: the consumer must match the toolchain the archive was built with (6.6);
  transitive vcpkg libraries are either re-resolved (same manifest, same
  baseline) or bundled (size).
- Repository is public `[verified]`, so `file(DOWNLOAD)` needs no token. If that
  ever changes, CMake's `HTTPHEADER "Authorization: Bearer ..."` or the services
  API upload the M2O release already uses (`createReleaseArchiveUpload`) are the
  fallbacks.

### Option B - vcpkg port in a MafiaHub registry

Publish a `mafiahub-framework` port in a git registry; mods list it in their
`vcpkg.json`; a shared binary cache makes it "prebuilt" in practice.

- Pros: transitive dependencies are ports depending on ports, so the manifest
  problem of 6.4 disappears; the triplet already fixes the CRT; version
  resolution is the registry baseline the mods already understand.
- Cons: a binary cache is not a release artifact - entries are evicted and are
  not hash-pinned by the consumer; every mod CI needs cache credentials; the
  Framework's own non-vcpkg downloads (MafiaNet, libnode, CEF, MafiaHubServices)
  have to run inside a port build, where network access is discouraged and the
  CEF cache directory logic does not apply; pointing a mod at a local Framework
  source tree means an overlay port with a filesystem path, which is clumsier
  than 8.3. Debug/Release variants come for free but double the port build.
- Verdict: a reasonable second step **after** option A exists, because the port
  can simply download the option A archive (a "binary port"). Not the first step.

### Option C - shared-library SDK (`Framework.dll`, `FrameworkClient.dll`, `FrameworkServer.dll`)

Hides every transitive dependency behind an exported C++ surface; one DLL set
serves Debug and Release consumers; toolchain coupling drops to the exported
surface.

- Costs that are certain: an export macro on every class and free function the
  mods reach (111 headers today, 248 to be safe); templates and inline functions
  stay header-side and still require the same third-party headers; `v8pp` class
  bindings are templates instantiated in both modules, so the scripting layer
  would need an explicit registration interface; `CoreModules` and every other
  function-local or namespace-scope singleton becomes "one per module" unless
  accessed only through exported accessors; Dear ImGui needs
  `SetCurrentContext`/`SetAllocatorFunctions` handed across the boundary;
  `spdlog`'s registry is per module; Tracy already handles this
  (`TRACY_DELAYED_INIT`) but every other static library in the closure would be
  duplicated if a mod also linked it.
- Costs that are specific to this project: `FrameworkClient` runs inside a
  PE-loaded game image under a launcher with `/FORCE:MULTIPLE`, `/DYNAMICBASE:NO`
  and a fixed `/BASE`; a second DLL in that process is a loader-order and
  address-space question the launcher does not have today. `FrameworkLoader` is
  **meant** to be static (it is the launcher's body) and stays so in any option.
- Verdict: the right end state for ABI isolation and the wrong first move.
  Everything in option A is a prerequisite for it anyway (install/export,
  runtime salt, interface definitions), so nothing is wasted by doing A first.

### Static closure: one archive per library or one merged archive?

With option A the mod still links around twenty static libraries (Framework
libs, MafiaNet + opus + rnnoise, httplib, semver, Hash, libsig, sfd, ftl,
TracyClient, physfs, miniaudio, udis86, ntdll, steamworks, discord, ImGuizmo,
libcef_dll_wrapper, MafiaHubServices, libnode/v8 import libs) plus the vcpkg set.
CMake's exported targets carry the list, so the mod writes
`target_link_libraries(X Framework::Client)` and nothing else. Merging into one
`.lib` (`lib.exe /OUT`) buys nothing but a smaller file list and loses per-library
PDB association; do not merge. The MafiaNet archive ships three libraries for the
same reason.

## 6. Package design (option A)

### 6.1 Archive layout

```
Framework-<version>-<platform>[-<variant>]/
  include/framework/            every header under code/framework/src, same relative paths
  include/framework-tests/unit.h
  include/<bundled third party>  mafianet/, cef (include/ + libcef_dll/), libnode (node/, uv/, v8), ftl, tracy, physfs, httplib, semver, hash, libsig, sfd, udis86, ntdll, steamworks, discord, imguizmo, miniaudio, services/
  lib/                          Framework, FrameworkClient, FrameworkServer, FrameworkLoader, FrameworkLoaderData import lib, bundled third-party archives, libnode import lib, libcef.lib, libnode_node_api.def
  lib/cmake/Framework/          FrameworkConfig.cmake, FrameworkConfigVersion.cmake, FrameworkTargets[-<config>].cmake, FrameworkSetup.cmake, modules/ (UpdateGitHash, GetGitRevisionDescription, FrameworkBinaryIdentity, templates/), FrameworkRuntime.cmake (fw_stage_runtime), FrameworkAbi.cmake
  bin/                          FrameworkLoaderData.dll, cef_subprocess.exe, epic_sign_in.exe, MafiaHubUpdater.exe, crashpad_handler, libnode.dll | libnode.so.*, libcef.dll + chrome_elf.dll + libEGL/libGLESv2 + d3dcompiler/dxcompiler/dxil, *.pak, icudtl.dat, locales/, discord_game_sdk.dll, fw_steam_api64.dll
  share/framework/              scripts/check_pattern_table.py (+ build_pattern_table.py), launcher.manifest, game_reserve.cpp, vcpkg.json + vcpkg-triplets/ + baseline (6.4), MANIFEST.json (versions of every bundled dependency, toolchain id, flags), LICENSE/NOTICE
```

Platforms: `windows-x64`, `windows-x86` (client-only: M2O's client is 32-bit and
the x86 build is V8-only), `linux-x86_64` (server-only). macOS stays a CI
compile check until a mod ships a macOS server; do not promise an archive nobody
consumes.

### 6.2 Exported targets

`Framework::Framework`, `Framework::Client`, `Framework::Server`,
`Framework::Loader`, `Framework::LoaderData` (SHARED IMPORTED, for
`$<TARGET_FILE>` staging), `Framework::Services`, and the bundled third-party
targets under their current names so mod CMake keeps working (`imgui::imgui`
and `Microsoft::DirectXTK` come back from `find_dependency`; `libnode`,
`CEF`, `MafiaNet::MafiaNetStatic` from the bundled export). Keep the unqualified
names as `ALIAS`es for one transition release so the first mod move is a
mechanical path change.

Interface properties the export has to carry, or the consumer's compile differs
from the archive's:

- `INTERFACE_COMPILE_DEFINITIONS`: `NOMINMAX`, `_USE_MATH_DEFINES` (Windows),
  `MG_ENABLE_LOG=0`, `TRACY_ENABLE`/`TRACY_ON_DEMAND`/`TRACY_DELAYED_INIT`/
  `TRACY_FIBERS` as built, `NODE_WANT_INTERNALS=1` on `Framework::Server` only
  if a shipped header still needs it (check: today it is `PRIVATE` on the node
  engine object library and set again by the tests), miniaudio's `MA_NO_*` set
  (already `PUBLIC` on the vendored target, so it exports as-is).
- `INTERFACE_LINK_OPTIONS` on `Framework::Loader`: the whole launcher link line
  (`/SAFESEH:NO /DYNAMICBASE:NO /LARGEADDRESSAWARE /FORCE:MULTIPLE /STACK:... /ENTRY:mainCRTStartup /LAST:.zdata /MANIFEST:EMBED /BASE:...`) and
  `/MANIFESTINPUT:` pointing at the installed `launcher.manifest` unless the
  target sets `FW_LAUNCHER_MANIFEST`; on `Framework::Server`: `/DEF:<installed
  libnode_node_api.def>`; on `Framework::LoaderData`: nothing (exports are in
  the DLL).
- `INTERFACE_SOURCES` on `Framework::Loader`:
  `$<INSTALL_INTERFACE:share/framework/game_reserve.cpp>` so
  `FW_LAUNCHER_GAME_RESERVE` keeps compiling into each launcher.
- `INTERFACE_INCLUDE_DIRECTORIES`: `$<INSTALL_INTERFACE:include/framework>` plus
  the bundled third-party include roots. Today every include directory is
  `$<BUILD_INTERFACE:...>` only, which is why `install(EXPORT)` would currently
  produce targets with no include paths; the generator expressions are half-done
  already.
- `MAP_IMPORTED_CONFIG_DEBUG Release` (and RelWithDebInfo, MinSizeRel) when the
  archive carries a single configuration, exactly as done for MafiaNet. This is
  legal here only because `FrameworkSetup.cmake` forces `/MD` and overrides
  `CMAKE_CXX_FLAGS_DEBUG` so Debug builds never define `_DEBUG` and
  `_ITERATOR_DEBUG_LEVEL` is 0 everywhere; the installed `FrameworkSetup.cmake`
  must keep that rule, and `FrameworkAbi.cmake` must assert it (6.6).

### 6.3 Variants

- `release`: `RelWithDebInfo` flags, `/Z7` instead of `/Zi` so debug information
  lives in the object files and ships inside the `.lib` with no PDB to carry;
  Sentry symbol upload keeps working because the mod's final link still produces
  one PDB per binary. `FW_NODE_INSPECTOR` off, Tracy on-demand on, injection off.
- `debug` (optional, Windows x64 only at first): same CRT, no optimisation,
  `FW_NODE_INSPECTOR` on. Exists so a mod developer can step into Framework code
  with a prebuilt SDK. Doubles the Windows build job; decide in phase 0 whether
  the source override (8.3) is enough instead. MafiaNet ships release only and
  nobody has asked for more.
- Never `/GL` or LTO in the archive: whole-program-optimised objects are bound
  to the exact toolset version and defeat the forward-compatibility rule in 6.6
  `[reported]`.

### 6.4 Transitive dependencies: bundle or find?

Two classes, two answers.

- **Non-vcpkg (bundle).** MafiaNet, libnode/V8, CEF (wrapper and runtime),
  MafiaHubServices and every in-tree vendor (ftl, tracy, physfs, httplib, semver,
  hash, libsig, sfd, udis86, ntdll, steamworks, discord, imguizmo, miniaudio).
  They are version-pinned by the Framework, several are patched or configured in
  ways a consumer could not reproduce (`TRACY_DELAYED_INIT`, `PHYSFS_ARCHIVE_*`,
  compiled-mode httplib for the services adapter), and bundling is what makes the
  mod configure download one archive instead of five.
- **vcpkg ports (find, with the SDK's manifest).** OpenSSL, curl, sentry-native,
  spdlog, fmt, glm, nlohmann, cppfs, miniz, cxxopts, function2, imgui, directxtk,
  minhook, stackwalker. `FrameworkConfig.cmake` uses `find_dependency` for each.
  The archive ships the exact `vcpkg.json`, `builtin-baseline` and triplets it was
  built with under `share/framework/`, and `FrameworkBootstrap.cmake` configures
  the mod's vcpkg from them (mod-specific additions go in the mod's own manifest
  that lists the SDK's as an overlay or is generated from it). The shared binary
  cache the mods already point `VCPKG_BINARY_SOURCES` at makes this a download,
  not a build.
  - Why not bundle them too: Debug `vcpkg_installed/lib` is 251 MB `[measured]`
    (release will be smaller; measure in phase 0), the ports have their own
    licences and `copyright` files to carry, and bundling freezes the OpenSSL
    the mod's own code links even when the mod needs a newer one. MafiaNet made
    the same call for OpenSSL.
  - Risk: a consumer whose vcpkg resolves a different port build than the
    archive saw (different baseline, different triplet, a feature flipped) links
    an ABI-incompatible spdlog or fmt. `FrameworkAbi.cmake` compares the
    consumer's `VCPKG_TARGET_TRIPLET` and baseline against `MANIFEST.json` and
    fails the configure on mismatch.
  - If phase 0 shows the release `vcpkg_installed` is small enough and the
    manifest coupling annoys, flip to bundling; the package layout does not
    change, only the Config file does.

### 6.5 Runtime staging

Replace the ad hoc copy targets (`CEFCopyDlls`, `libnode_copy_dll`, the
`crashpad_handler` `file(COPY)`, every mod's `copy_if_different` of
`libnode.so`) with one installed function:

`fw_stage_runtime(<target> [CLIENT|SERVER|LAUNCHER])` adds `POST_BUILD` copies of
the matching `bin/` subset next to `<target>`, honours `$ORIGIN` RPATH on Linux
(already set per mod; move it here), and is also what the release workflows'
"stage bundle" steps call so the file list lives in one place. Mods keep their
own asset copies (`files/`, `resources/`, `assets/`).

### 6.6 Toolchain pin and ABI check

- **Windows.** The archive is built with a stated MSVC toolset (record `_MSC_VER`
  and `_MSC_FULL_VER` in `MANIFEST.json`). Consumers must use the same major
  toolset (v143 lineage) and a version **equal or newer**; MSVC guarantees that
  libraries built with an older 14.x toolset link under a newer one, not the
  reverse `[reported]`. The release job should therefore build with the oldest
  toolset the team still runs, or pin the runner's VS version and tell developers
  the floor. `/MD`, `_ITERATOR_DEBUG_LEVEL=0`, C++20, `/Zc:__cplusplus` and
  pointer size are checked at configure time by `FrameworkAbi.cmake` compiling a
  variant of `scripts/windows-container/toolchain-check.cpp` with `try_compile`
  against values from `MANIFEST.json`; a `framework/abi.h` `static_assert`s the
  same from inside the first included header so a hand-written build fails
  loudly too.
- **Linux.** Static libraries are tied to the libstdc++ ABI and reasonably to
  the GCC major. Build the archive in the same `ubuntu:24.04` image the mods'
  server Dockerfiles start from (fmp, m2o, hogwarts all do), record the compiler
  id and `_GLIBCXX_USE_CXX11_ABI`, and check them the same way.
- **Architecture.** Separate archives; the x86 one has no server libraries and
  the Config file says so (`Framework_HAS_SERVER` false) instead of exporting a
  target that cannot exist.

### 6.7 Versioning and pinning

- The Framework version and its MAJOR/MINOR/PATCH semantics do not change.
  `bump_version.sh` keeps classifying by path; add `cmake/FrameworkConfig.cmake.in`
  and the export list to the **major** paths, because a change there breaks every
  consumer's configure the same way a wire change breaks every peer.
- `NetworkPeer::BuildToken` folds `Utils::Version::rel` of the Framework; in the
  SDK that string is compiled into the archive's `version.cpp`, so the token keeps
  meaning "the Framework the binary contains". No change.
- `FRAMEWORK_PIN` becomes `FrameworkPin.cmake` in each mod, shaped exactly like
  `cmake/MafiaNetPin.cmake`: `set(FRAMEWORK_PIN_VERSION "38.0.0")` and one
  `set(FRAMEWORK_PIN_SHA256_<platform> ...)` per archive the mod consumes. Plain
  `set()`, not cache entries, for the reason that file gives. The release job
  prints the hashes; a `scripts/bump_framework_pin.py` in the mod can fetch and
  rewrite them.
- The `framework_ref` dispatch override in the mods' release workflows becomes a
  `framework_sdk_url` (or a local path for the source mode) and keeps its
  "testing only, disables promotion" semantics.

## 7. Framework changes required

Ordered so each step is buildable and testable inside this repository before any
mod moves.

### 7.1 Install and export (`code/framework/CMakeLists.txt`, `vendors/`, new `cmake/FrameworkConfig.cmake.in`)

- Add `$<INSTALL_INTERFACE:...>` beside every `$<BUILD_INTERFACE:...>` include
  directory; move the `PRIVATE` vendor include paths that leak through public
  headers (ftl, semver, physfs are `PRIVATE` today yet `jobs/*.h` includes ftl) to
  the interface or fix the headers.
- `install(TARGETS Framework FrameworkClient FrameworkServer FrameworkLoader FrameworkLoaderData cef_subprocess epic_sign_in EXPORT FrameworkTargets ...)`
  plus the in-tree vendor targets they link (`httplib semver Hash libsig sfd ftl TracyClient physfs-static miniaudio udis86 ntdll SteamSDK DiscordSDK ImGuizmo libcef_dll_wrapper`), each of which needs an `install(TARGETS)` of its own or a `PUBLIC_HEADER`/`install(DIRECTORY)` for its headers. Several vendored `CMakeLists` are upstream files; prefer `install()` calls in `vendors/CMakeLists.txt` over editing them.
- Imported targets cannot be installed through `install(EXPORT)`: MafiaNet,
  libnode/v8, `libcef`, MafiaHubServices and the `v8_monolith` archive are
  re-declared in `FrameworkConfig.cmake.in` as `IMPORTED` with paths relative to
  `${PACKAGE_PREFIX_DIR}`; `install(FILES/DIRECTORY)` copies their files into
  `lib/`, `bin/`, `include/`.
- `MafiaHubServices` and `Framework` reference each other (the "link Framework
  twice" in every server CMake). In the export, put both in one
  `$<LINK_GROUP:RESCAN,...>` on `Framework::Server`'s and `Framework::Loader`'s
  interface (CMake 3.24, already the floor), so consumers stop repeating the
  trick.
- Export `FrameworkSetup.cmake` unchanged in content but **scoped**: it currently
  sets global `CMAKE_*` variables and `add_compile_options`; the installed copy
  does the same for the mod (that is the intent: one flag policy) and is included
  by `FrameworkBootstrap.cmake` right after `project()`.
- Install `cmake/UpdateGitHash.cmake`, `GetGitRevisionDescription.cmake(.in)`,
  `FrameworkBinaryIdentity.cmake`, `templates/binary_identity.rc.in`,
  `scripts/check_pattern_table.py`, `scripts/build_pattern_table.py`,
  `code/tests/unit.h`, the generated `launcher.manifest`,
  `src/launcher/game_reserve.cpp`, `libnode_node_api.def`, `vcpkg.json`,
  `cmake/vcpkg-triplets/`.
- Write `MANIFEST.json` at install time: Framework version and commit, every
  bundled dependency's version (MafiaNet pin, libnode, CEF, services
  `build.txt`), vcpkg baseline and triplet, compiler id and version, the
  `INTERFACE_COMPILE_DEFINITIONS` set.
- Acceptance: `cmake --install builds/build-release-64 --prefix <stage>` then a
  throwaway consumer in `code/tests/sdk-consumer/` (a minimal server and, on
  Windows, a minimal client DLL and launcher) configures against `<stage>` with
  **no** access to the Framework source tree and links. That consumer becomes
  the permanent smoke test of the export.

### 7.2 Runtime staging function (`cmake/FrameworkRuntime.cmake`, installed)

Per 6.5. Also used in-tree so the file list has one owner; `CEFCopyDlls` and
`libnode_copy_dll` are removed once nothing references them.

### 7.3 RPC identifier salt at runtime (MAJOR)

Today: `rpc_identifier.h` derives every identifier `constexpr` from
`FW_RPC_IDENTIFIER_SALT`; release CI passes a per-tag salt derived from a secret;
`BuildToken` folds `IdentifierSaltTag()` so mismatched peers fail the connection
gate. With a prebuilt Framework the identifiers compiled into
`replication_manager.cpp` and the framework-side `rpc/*.h` would carry the
default salt while the mod's would carry the release salt, and nothing would
dispatch.

Change: the salt becomes a value handed to `NetworkPeer` at construction (from
`Instance` options, which the mod fills from its own compile-time or runtime
secret), identifiers are computed when an RPC is registered or sent (SipHash of
a `std::string_view` is already there; it just stops being `constexpr`), and
`IdentifierSaltTag()` reads the runtime value. Costs: one hash per registration
and per send unless identifiers are cached at registration (they should be;
RPC4 keys on the string anyway). Classification: touches
`code/framework/src/networking/rpc`, so `bump_version.sh` makes it MAJOR
automatically; every mod re-pins. The mods' release workflows then stop passing
`-DFW_RPC_IDENTIFIER_SALT` to the Framework and start passing it to their own
target (or move the secret into their config); that is a one-line change per
workflow and is independent of the SDK move, so land it first, on its own.

### 7.4 Options that become archive properties

`FW_DLL_INJECTION` stays an option but is documented as source-build only (it
changes header layout). `FW_NODE_INSPECTOR`, `FW_PROFILING` and the Tracy set
are recorded in `MANIFEST.json` and exported as interface definitions where a
header depends on them. `LIBNODE_V8_ONLY` is fixed per archive.

### 7.5 Release workflow (`.github/workflows/release_sdk.yml`)

Triggered by the tag `pr_tag_commit.yml` pushes (or chained after it with
`workflow_run`). Matrix: `windows-x64`, `windows-x86`, `linux-x86_64`, each
configure + build + `cmake --install` + archive + SHA-256, then one job that
uploads the archives and a `SHA256SUMS` file to the release
`softprops/action-gh-release` already created, and appends the hash block to the
release body so a pin can be copied from it. Runners: the self-hosted Windows
runners the mods already use (they carry the toolset the archive must pin) and
the `ubuntu:24.04` container for Linux. Build caches: the same sccache store and
`VCPKG_BINARY_SOURCES` the mods use; the Framework build is 134 edges and is the
part that caches best.

Add a canary job (manual or nightly) that dispatches one mod's release workflow
with `framework_sdk_url` pointing at the fresh archive, so a Framework change that
breaks the mods is seen before the mods re-pin.

### 7.6 Removing the in-tree projects

Once every active mod has moved: delete the `projects/*` glob from
`code/CMakeLists.txt`, delete `FW_STANDALONE` (it is unconditionally true and
only guards that glob), drop `code/projects/` from `builds/build.bat`'s examples,
rewrite `docs/starting_a_new_project.md` (sections "A project is registered by
existing" and the four-file skeleton), `docs/project_architecture.md` where it
says "built as a subdirectory of the Framework tree", `AGENTS.md`/`CLAUDE.md`
build sections, and `CMakeSettings.json`. `builds\build.bat` keeps building
Framework targets and tests; the mods get their own copy of the script (or a
shared `FrameworkBuild.cmake` invoked the same way) so the "one supported build
entry point" rule survives in each repository.

## 8. Mod-side changes

### 8.1 `FrameworkBootstrap.cmake` (vendored into each mod, like `VcpkgBootstrap.cmake` is here)

Runs before `project()`, mirroring `cmake/VcpkgBootstrap.cmake` and the MafiaNet
block:

1. Read `FrameworkPin.cmake`. Pick the platform archive from
   `CMAKE_SYSTEM_NAME`/pointer size/`VSCMD_ARG_TGT_ARCH` the way
   `VcpkgBootstrap.cmake` picks the triplet.
2. If `FW_SDK_ROOT` is set (a local install tree) use it; else if
   `FW_SOURCE_DIR` is set, enter source mode (8.3); else download
   `https://github.com/MafiaHub/Framework/releases/download/v<ver>/Framework-<ver>-<platform>.<ext>`
   into `${FW_SDK_CACHE_DIR}` (default `${CMAKE_BINARY_DIR}/framework-sdk`,
   overridable by `-D` or environment like `FW_CEF_CACHE_DIR`) with
   `EXPECTED_HASH SHA256=...`, extract once.
3. Point vcpkg at the SDK's manifest and triplets (6.4), set
   `CMAKE_TOOLCHAIN_FILE`, append `lib/cmake/Framework/modules` to
   `CMAKE_MODULE_PATH`, `set(Framework_DIR ...)`.
4. After `project()`: `include(FrameworkSetup)`, `find_package(Framework
   CONFIG REQUIRED)`, which runs `FrameworkAbi.cmake`.

### 8.2 CMake and CI edits per mod

- Root `CMakeLists.txt`: `include(cmake/FrameworkBootstrap.cmake)` before
  `project()`; drop `include_directories(.../code/framework/src/)`.
- `code/CMakeLists.txt`: drop `include_directories(${CMAKE_SOURCE_DIR}/code/framework ...)`;
  `include(UpdateGitHash)` keeps working from the installed module path.
- Client/server/launcher: `Framework` -> `Framework::Framework` and so on (or
  keep the aliases for the first release); replace copy commands with
  `fw_stage_runtime`; `${CMAKE_SOURCE_DIR}/scripts/check_pattern_table.py` ->
  `${Framework_SCRIPTS_DIR}/check_pattern_table.py`; tests include
  `include/framework-tests`.
- `.github/workflows/release.yml` (M2O shape; hogwarts/KCD2MP/fmp are the same
  template): remove the Framework checkout and the nested `code/projects/<mod>`
  path, check the mod out at the root, configure the mod directly, drop
  `-DFW_RPC_IDENTIFIER_SALT` in favour of the mod-side salt from 7.3, keep
  sccache and the vcpkg cache, keep every stage/verify/upload step. The
  "Resolve Framework pin" step validates `FrameworkPin.cmake` (version string
  and hashes) instead of a 40-character SHA; the override input becomes a
  URL or path.
- `Dockerfile`s are untouched: they already copy a staged `runtime/` directory.
- `FRAMEWORK_PIN`'s history of why-each-bump comments is valuable; carry it into
  `FrameworkPin.cmake` as comments the same way `MafiaNetPin.cmake` does.

### 8.3 Developer loop: source override

`-DFW_SOURCE_DIR=E:/MafiaHub/Framework` makes `FrameworkBootstrap.cmake`
`add_subdirectory(${FW_SOURCE_DIR} framework)` instead of `find_package`, after
including that tree's `VcpkgBootstrap.cmake` and `FrameworkSetup.cmake`. The
Framework's own `CMakeLists.txt` must then tolerate being a subdirectory: `project()`
stays, the `code/projects` glob is gone by then, and `FW_STANDALONE` is no longer
needed. The exported target names are the same `Framework::*` aliases in both
modes, so mod CMake does not branch. This is the MafiaHubServices
`BUILD_FROM_SOURCE` pattern applied one level up, and it is what keeps
"change a Framework header, rebuild the mod" at one incremental build. Document
it in each mod's `CLAUDE.md`/`AGENTS.md` build section next to the build script.

## 9. Compatibility policy to publish with the first SDK

- A mod declares exactly one Framework version; mixing archives is undefined.
- MAJOR/MINOR/PATCH keep their meaning from `AGENTS.md`; additionally, a change
  to the exported CMake surface (target names, interface definitions, bundled
  dependency majors) is MAJOR.
- Windows consumers: MSVC v143-lineage toolset, version >= the one in
  `MANIFEST.json`; `/MD`; C++20. Linux consumers: GCC major recorded in the
  manifest, same libstdc++ ABI. Enforced by `FrameworkAbi.cmake`, not by prose.
- The archive carries no `/GL` objects and no `_DEBUG` CRT; Debug mod builds
  link the release archive (as they link the release MafiaNet and vcpkg libraries
  today).
- Archives are immutable; a bad release gets a new PATCH, never a re-upload.

## 10. Risks and open decisions

1. **Debug variant or source override only?** Decide after phase 0 measures the
   Windows release job; the override covers the debugging use case, the variant
   covers "step into the SDK without a checkout".
2. **Bundle vcpkg libraries or re-resolve?** Default re-resolve (6.4); flip if the
   manifest coupling bites in the first mod move or the release
   `vcpkg_installed` turns out small.
3. **ODR across the boundary.** Any Framework header whose layout depends on a
   macro the consumer could set differently is a silent crash. Known today:
   `FW_DLL_INJECTION` (shipped OFF, documented), miniaudio `MA_NO_*` (exported).
   Phase 1 adds a grep-based check in the Framework CI for `#if` on `FW_*`,
   `TRACY_*`, `MA_*`, `NODE_*` inside `code/framework/src/**/*.h`.
4. **Toolset drift on self-hosted runners.** The archive's toolset floor moves
   whenever the runner updates Visual Studio; developers on older VS stop
   linking. Pin the runner's toolset (`VCToolsVersion`) or accept "update VS".
5. **Framework developers lose the one-tree convenience.** Two checkouts and
   `-DFW_SOURCE_DIR` replace it. Worth stating in the PR; the glob was also what
   made every Framework configure list eight projects it did not need.
6. **Mods that read Framework internals outside headers.** None compile Framework
   `.cpp` files today `[verified]`, but hogwarts/KCD2MP tests reach
   `code/tests/unit.h` and three mods run a Framework script; both are shipped.
   Re-grep before each mod moves.
7. **x86.** Only M2O needs it; if M2O's 32-bit client is ever retired the x86
   archive goes with it. Until then it doubles the Windows matrix.
8. **MafiaHubServices rolling release.** The SDK bundles whatever adapter build
   its release job downloaded; the manifest records the `build.txt` stamp so the
   services version inside a given SDK is at least knowable. Making that adapter
   version-pinned is a separate, smaller task worth doing alongside.
9. **Option B/C later.** Both sit on top of A. Do not design A around them beyond
   what sections 6.2 and 7.1 already require.

## 11. Staged plan

Each phase ends in a state where every mod still builds, either in-tree or
against the SDK.

| Phase | Work | Where | Done when |
|---|---|---|---|
| 0 - Measure | Release-config sizes of the Framework libs, bundled vendors and `vcpkg_installed`; MSVC and GCC versions on the runners; a `cmake --install` dry run listing what has no install rule | Framework | Numbers in this document's appendix; decisions 1 and 2 of section 10 taken |
| 1 - Runtime salt | 7.3; mods' release workflows pass the salt to their own targets | Framework (MAJOR), then each mod's workflow | A mod built against the new Framework in-tree connects with a per-tag salt |
| 2 - Install/export | 7.1, 7.2, `FrameworkAbi.cmake`, `MANIFEST.json`, `code/tests/sdk-consumer` | Framework | Consumer links against an installed prefix on Windows x64/x86 and Linux with no source tree |
| 3 - Publish | 7.5 release job; hashes in release body; first `Framework-<ver>-*` archives on a tag | Framework | Archive downloadable, hash-pinned, consumer smoke test runs from the download in CI |
| 4 - First mod | `FrameworkBootstrap.cmake`, `FrameworkPin.cmake`, 8.2 edits, 8.3 override, release workflow rewrite; M2O first (baseline project, has the x86 client, the launcher, Linux server and the most elaborate release pipeline) | M2O | M2O prerelease tag builds client, server and Linux server from the SDK; promoted release ships |
| 5 - Remaining mods | hogwarts, KCD2MP, fmp, MafiaMP, m3o, cybermp, tmp (decide which of the inactive ones move and which are archived with a note) | each mod | Every active mod releases from the SDK at least once |
| 6 - Cut the cord | 7.6: delete the glob and `FW_STANDALONE`, rewrite docs and build scripts, canary job on | Framework | Framework repository has no reference to `code/projects` |
| 7 - Later | Option B binary port if a mod wants pure-vcpkg consumption; option C once a stable ABI is actually needed | - | - |

Phases 1 and 2 are independent of each other and can run in parallel; 3 depends
on 2; 4 depends on 1 and 3.

## 12. Status `[2026-10-25]`

Implemented in the tree, uncommitted at the time of writing:

- **Phase 1 done.** `rpc_identifier.h` digests the name at compile time
  (`FW_RPC_IDENTIFIER` yields `RPC::Identifier`) and `NetworkPeer` salts it at
  runtime (`SetIdentifierSalt`, `ResolveIdentifier`, re-keying registered
  slots); `InstanceOptions::rpcIdentifierSalt` on both sides;
  `BuildToken(..., salt)`. The Framework no longer compiles
  `FW_RPC_IDENTIFIER_SALT`; M2O reads that cache variable itself and defines
  `M2O_RPC_IDENTIFIER_SALT` on its targets. Payload declarations became
  `static constexpr auto kIdentifier = FW_RPC_IDENTIFIER(...)`. Tests pass.
  Other mods keep building against their pinned commit; when they re-pin they
  must apply the salt to their own targets (the Framework only prints a STATUS
  line now).
- **Phase 2 done on Windows x64.** `cmake/FrameworkInstall.cmake` (helpers and
  `fw_sdk_finalize()`), `FrameworkConfig.cmake.in`, `FrameworkAbi.cmake`,
  `FrameworkRuntime.cmake` (`fw_stage_runtime`), generated
  `FrameworkImported.cmake`/`FrameworkManifest.cmake`/`MANIFEST.json`;
  `install(EXPORT)` of `Framework::*` plus the vendored targets; MafiaNet and
  Tracy consumed through their own packages inside the prefix; runtime split
  into `runtime/client` and `runtime/server`. The project glob is
  `EXCLUDE_FROM_ALL` so `cmake --install` of the tree yields the SDK alone.
  `builds\build.bat FrameworkSdkSmoke 64` installs to `builds/build-64/sdk`
  and builds `code/tests/sdk-consumer` (server exe, client DLL, launcher exe)
  against it; both binaries run. Debug install tree: include 18 MB, lib
  464 MB, runtime 619 MB (CEF dominates), mafianet 30 MB.
- **Phase 3 written, not exercised.** `.github/workflows/release_sdk.yml`
  chains on `Version Release` (`workflow_run`, since the tag is pushed with
  `GITHUB_TOKEN`), builds `windows-x64`, `windows-x86`, `linux-x86_64`,
  smoke-tests the archive and uploads it with `SHA256SUMS` and a pin block in
  the release body. `bump_version.sh` treats `FrameworkConfig.cmake.in` and
  `FrameworkInstall.cmake` as MAJOR paths.
- **Phase 4 started on M2O.** `cmake/FrameworkBootstrap.cmake` (modes:
  `nested`, `source`, `sdk`, pinned download) and `FrameworkPin.cmake` (version
  only until the first archive exists), own `builds/build.bat`, `Framework::*`
  link lines, `fw_stage_runtime`, `Framework_SCRIPTS_DIR`. M2OServer builds in
  all three modes; the 32-bit client needs an x86 SDK, which no local tree
  produces yet. `release.yml` still builds nested and is the next step once an
  archive exists.
- **Layout revised to one archive per OS.** `lib/<arch>`, `runtime/<arch>`,
  `mafianet/<arch>`, `share/framework/<arch>` are disjoint; headers, scripts and
  the entry `lib/cmake/Framework/FrameworkConfig.cmake` (which dispatches on
  pointer size to `lib/<arch>/cmake/Framework`) are shared. The x86 and x64
  install trees overlay into `Framework-<ver>-windows.zip`; Linux ships
  `Framework-<ver>-linux.tar.gz`. Verified locally by overlaying
  `builds/build/sdk` and `builds/build-64/sdk` and building M2OClient,
  M2OLauncher (x86) and M2OServer (x64) against the result. `Version Release`
  runs for `develop` and `next` (a `next` release is a prerelease) and
  dispatches `release_sdk.yml` for the tag.
- Not done: Linux archive exercised, M2O `release.yml`, phases 5 and 6, the
  Debug-variant and bundle-vcpkg decisions of section 10.

## Appendix A - measurements `[measured 2026-10-24, builds/build-64, Debug]`

| Artifact | Size |
|---|---|
| `Framework.lib` | 153 MB |
| `FrameworkClient.lib` | 82 MB |
| `FrameworkServer.lib` | 45 MB |
| `FrameworkLoader.lib` | 25 MB |
| `libcef_dll_wrapper.lib` | 91 MB |
| `vcpkg_installed/x64-windows-mh/lib` | 251 MB |
| libnode 25.6.1 `lib/` (import libs + `libnode.dll`) | 120 MB |
| `bin/` with CEF runtime, mods and symbols | 1.7 GB |

Debug objects without optimisation inflate these; the release numbers are the
ones that matter and are phase 0's first task.

Framework cadence: 359 commits and 61 `VERSION` bumps between 2026-07-24 and
2026-10-24; a GitHub release per bump, no assets; latest `v37.0.4`.

Header surface: 248 headers, 111 used by mods (59 M2O, 60 hogwarts).

## Appendix B - files this plan touches in the Framework

`CMakeLists.txt`, `code/CMakeLists.txt`, `code/framework/CMakeLists.txt`,
`code/framework/src/services/CMakeLists.txt`, `vendors/CMakeLists.txt`,
`vendors/cef/CMakeLists.txt`, `vendors/libnode/CMakeLists.txt`,
`cmake/FrameworkSetup.cmake`, `cmake/NodeApiExports.cmake`, new
`cmake/FrameworkConfig.cmake.in`, `cmake/FrameworkRuntime.cmake`,
`cmake/FrameworkAbi.cmake`, `code/framework/src/networking/rpc/rpc_identifier.h`,
`code/framework/src/networking/rpc/rpc.h`, `code/framework/src/networking/network_peer.{h,cpp}`,
`code/framework/src/integrations/{client,server}/instance.{h,cpp}`,
`code/tests/CMakeLists.txt` (+ `sdk-consumer/`), `.github/bump_version.sh`,
`.github/workflows/pr_tag_commit.yml`, new `.github/workflows/release_sdk.yml`,
`builds/build.bat`, `CMakeSettings.json`, `AGENTS.md`, `CLAUDE.md`,
`docs/starting_a_new_project.md`, `docs/project_architecture.md`.
