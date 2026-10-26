<div align="center">
   <a href="https://github.com/MafiaHub/Framework"><img src="https://github.com/MafiaHub/Framework/assets/9026786/43e839f2-f207-47bf-aa59-72371e8403ba" alt="MafiaHub" /></a>
</div>

<div align="center">
    <a href="https://discord.gg/eBQ4QHX"><img src="https://img.shields.io/discord/402098213114347520.svg" alt="Discord server" /></a>
    <a href="LICENSE.txt"><img src="https://img.shields.io/badge/License-MafiaHub%20OSS-blue" alt="license" /></a>
    <a href="https://github.com/MafiaHub/Framework/releases/latest"><img src="https://img.shields.io/github/v/release/MafiaHub/Framework?include_prereleases" alt="Latest release" /></a>
</div>

<br />
<div align="center">
  An opinionated C++ framework for building multiplayer modifications for AAA games.
</div>

<div align="center">
  <sub>
    Brought to you by <a href="https://github.com/Segfaultd">@Segfault</a>,
    <a href="https://github.com/zpl-zak">@zpl-zak</a>,
    <a href="https://github.com/DavoSK">@DavoSK</a>,
    and other <a href="https://github.com/MafiaHub/Framework/graphs/contributors">contributors</a>!
  </sub>
</div>
<hr/>

## Introduction

The Framework is the shared foundation behind every MafiaHub multiplayer mod. A mod brings the game-specific part (hooks, the game's entities, its UI); the Framework supplies everything that stays the same from one game to the next:

- **Networking** - client/server transport on [MafiaNet](https://github.com/MafiaHub/MafiaNet) (our fork of RakNet/SLikeNet), connection admission and salted RPC identifiers.
- **Replication** - native entity replication (ReplicaManager3 + RPC4) with an interest grid for world streaming, on top of a flecs ECS.
- **Scripting** - JavaScript/TypeScript gamemodes: Node.js on the server, sandboxed V8 on the client.
- **GUI** - web UI through CEF (Chromium Embedded Framework) and in-game tooling through Dear ImGui.
- **Voice** - proximity voice chat; the server relays Opus frames without decoding them.
- **Jobs** - an opt-in fiber job system (FTL).
- **Launcher** - maps the game into its own process and brings the client DLL up with it.
- **Platform integrations** - Steam, Epic, Discord presence, Sentry crash reporting, Tracy profiling.
- **Integrations** - ready-made client and server `Instance` classes wiring all of the above together, with `PostInit` / `PostUpdate` / `PreShutdown` / `ModuleRegister` hooks for the game-specific part.

**MafiaHub Services** (masterlist, accounts) are not part of this repository, but the Framework integrates with them. Ask us on Discord for access and a license.

## How projects use the Framework

The Framework ships as a **prebuilt SDK**. A mod lives in its own repository and links a released Framework; it is no longer cloned into this tree and built alongside it.

```
 Framework repo                          Mod repo
 ──────────────                          ────────
 VERSION ─► git tag v<VERSION>           cmake/FrameworkPin.cmake        version + archive hashes
               │                         cmake/FrameworkBootstrap.cmake
               ▼                                     │
 GitHub release                                      │ first configure:
   Framework-<ver>-windows.zip   (x86 + x64)  ◄──────┘ download, verify, extract
   Framework-<ver>-linux.tar.gz
   SHA256SUMS
```

The mod's `CMakeLists.txt` includes the bootstrap before `project()` and links the exported targets:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FrameworkBootstrap.cmake)
project(MyMP CXX)
fw_bootstrap_finish()

target_link_libraries(MyMPServer PRIVATE Framework::Server)
fw_stage_runtime(MyMPServer SERVER)     # libnode and the crash handler next to the binary

target_link_libraries(MyMPClient PRIVATE Framework::Client)
fw_stage_runtime(MyMPClient CLIENT)     # CEF and the helper executables next to the DLL

target_link_libraries(MyMPLauncher PRIVATE Framework::Loader MyMPClient)
```

The bootstrap takes the Framework from the first source that is set:

| Source | Use it for |
| --- | --- |
| `-DFW_SOURCE_DIR=<Framework checkout>` | Working on the Framework and a mod together: the Framework builds from source as a subdirectory, so a Framework edit is one incremental build. |
| `-DFW_SDK_ROOT=<install tree>` | Testing a locally installed SDK (`cmake --install` of a Framework build). |
| `cmake/FrameworkPin.cmake` | Everything else: the pinned release, downloaded once into `FW_SDK_CACHE_DIR` and checked against its SHA-256. |

Every release's notes carry the `FrameworkPin.cmake` block ready to paste. Raising the pin is a release-affecting change for the mod: crossing a MAJOR version changes the network build token, so its client and server must move together.

Each mod passes its own RPC identifier salt at runtime (`InstanceOptions::rpcIdentifierSalt`, usually from a `FW_RPC_IDENTIFIER_SALT` release secret). Peers on different salts are refused at the connection gate.

To start a new mod, read [Starting a New Project](docs/starting_a_new_project.md) and [Project Architecture](docs/project_architecture.md).

## Building the Framework

You only need this to work on the Framework itself; mods do not build it.

We use CMake on **Windows**, **Linux** and **macOS**. Dependencies come from [vcpkg](https://vcpkg.io), fetched and bootstrapped automatically on the first configure. The few that are not on vcpkg live in `vendors/`, and CEF is downloaded at configure time.

```sh
git clone https://github.com/MafiaHub/Framework.git
cd Framework
```

### Windows

Use the build script. It loads the Visual Studio 2022 build environment and drives the canonical build tree in `builds/build-<arch>`:

```bat
builds\build.bat FrameworkTests 64
builds\build.bat RunFrameworkTests 64
builds\build.bat FrameworkSdkSmoke 64
```

`FrameworkSdkSmoke` installs the SDK into `builds\build-64\sdk` and builds a mod-shaped consumer (`code/tests/sdk-consumer`) against that prefix alone. Run it after touching the install rules.

Visual Studio 2022 (with the CMake tools) and CLion open the repository folder directly; see the [CLion guide](.github/CLION_GUIDE.md). To build Windows targets from Linux with the real MSVC toolchain, see the [MSVC/Wine container workflow](docs/windows-container.md).

### Linux and macOS

On Debian/Ubuntu, install the system development packages first (Fedora/RHEL: `openssl-devel`, `zlib-devel`, `libssh2-devel`, `libcurl-devel`):

```sh
sudo apt install build-essential cmake \
    libssl-dev zlib1g-dev libssh2-1-dev libcurl4-openssl-dev
```

```sh
cmake -B build
cmake --build build
cmake --build build --target RunFrameworkTests
```

The launcher (`Framework::Loader`) is Windows-only.

## Releases and versioning

Versions are set by hand. Raise `VERSION` in a commit, then push the matching tag:

```sh
git tag v41.0.0 && git push origin v41.0.0
```

The `Release` workflow checks the tag against `VERSION` and creates the GitHub release; a suffixed tag such as `v41.0.0-rc.1` is a prerelease. It then builds the windows-x64, windows-x86 and linux-x86_64 SDKs, smoke-tests each install tree and attaches the archives with their `SHA256SUMS`.

Which part to raise:

- **MAJOR** - netcode, shared ECS modules or the sync flow. Clients and servers must update together.
- **MINOR** - the scripting layer.
- **PATCH** - anything that does not affect peer sync or scripting.

## Documentation

Design notes for individual systems live in [`docs/`](docs), for example [connection admission](docs/connection_admission.md), [RPC identifiers](docs/rpc_identifiers.md), [replication transport](docs/replication_transport.md), [the CEF subprocess](docs/cef_subprocess.md) and [resource hot reload](docs/resource_hot_reload.md).

## Contributing

We're always looking for contributors. Reach us on [Discord](https://discord.gg/c6gW9yRXZH) or open an issue. Before sending a change, read the [Contribution Guidelines](.github/CONTRIBUTING.md) and the [Coding Style](.github/CODING_STYLE.md), and format with `scripts/format_codebase.sh`.

## License

The code is licensed under the [MafiaHub OSS](LICENSE.txt) license.

Its 5th clause keeps work on shared functionality in this repository: improvements go into the Framework rather than into diverging copies, so every project built on it benefits.
