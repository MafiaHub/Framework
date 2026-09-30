# Windows builds from Linux using MSVC and Wine

This optional builder runs the **Windows executables** for CMake, Ninja, Git,
vcpkg, PowerShell, Python and the Microsoft VS 2022 compiler/linker inside a Linux container through
Wine. It does not use MinGW or clang-cl. Both CMake's host and target are Windows,
so the existing Windows targets, precompiled libraries and vcpkg ports are used.

The container builds x64 and x86 targets. The wrapper defaults to x64; pass `32`
as its third argument for x86. Opt in by using the wrapper below;
no CMake platform override is needed because Windows CMake itself runs under Wine.
The native Linux/macOS configuration is unchanged. Native Windows builds can use
`builds\build.bat <target> <32|64> [Debug|Release|RelWithDebInfo]`.

For running the resulting launcher with Linux Steam, see
[Starting a launcher through GE-Proton](proton-launcher.md).

## Build

Requirements: an x86-64 Linux host and Docker, or rootless Podman. The image build
downloads Microsoft's build tools and SDK under their license using the pinned
[msvc-wine downloader](https://github.com/mstorsjo/msvc-wine). Build this image
locally; do not publish an image containing the Microsoft toolchain.

From the framework root:

```sh
scripts/build_windows_container.sh image
# Optional compiler/linker/runtime check before the full dependency build:
scripts/build_windows_container.sh ToolchainCheck
scripts/build_windows_container.sh SampleLauncher
scripts/build_windows_container.sh SampleClient
# 32-bit client using the x64-hosted x86 compiler:
scripts/build_windows_container.sh SampleClient Debug 32
# Optional server and framework tests:
scripts/build_windows_container.sh SampleServer
scripts/build_windows_container.sh RunFrameworkTests
```

These sample target names are placeholders: replace them with targets provided
by your project under `code/projects/`. The first build installs all
Windows vcpkg dependencies and fetches MafiaNet, Node/V8, CEF and MafiaHub Services.
It needs network access and substantially more time and storage than an
incremental build. Tests run under Wine; they do not validate game integration
on native Windows.

For Podman, prefix commands with `FW_CONTAINER_ENGINE=podman`. That mode uses a
separate VFS container store under `_external/msvc-wine/`, leaving existing
Podman images and containers alone. VFS consumes more disk space than an overlay
store. Docker uses its normal daemon storage. Set `FW_WINDOWS_IMAGE` to override
the local image tag.

Optional CMake definitions can be passed through `FW_CMAKE_ARGS`, for example
`FW_CMAKE_ARGS='-DFW_PROFILING=OFF' scripts/build_windows_container.sh SampleClient`.
Build parallelism defaults to eight jobs; set `FW_BUILD_JOBS` to override it for
both vcpkg and the project build.

```sh
FW_CONTAINER_ENGINE=podman scripts/build_windows_container.sh image
FW_CONTAINER_ENGINE=podman scripts/build_windows_container.sh SampleLauncher Release
```

## Run the Windows server

After building the server, run:

```sh
scripts/run_windows_server.sh
```

The script finds the single `*Server.exe` in `builds/build-64/bin`, reuses the
container image and detects the repository's Podman store automatically. Steam
is not needed. Stop the server with **Ctrl+C**.

Server arguments pass through directly, for example
`scripts/run_windows_server.sh --port 28015`. To select a different build, use
`scripts/run_windows_server.sh --exe builds/build-prod-64/bin/SampleServer.exe`.
Configuration and logs use the executable's directory. The server uses host
networking and its own Wine prefix, separate from the build prefix.

## Files and build directories

The entire repository is mounted at `/workspace`, exposed as `W:\` inside Wine.
All checkout writes use the calling user's identity. The runner does not mount
the user's home directory or game installation. Wine state, vcpkg buildtrees,
downloads and binary caches persist under `_external/msvc-wine/`.
The Windows console output is streamed to the terminal and retained in
`_external/msvc-wine/console.log` (replaced on each invocation).

The container invokes the same `builds\build.bat` entry point and canonical trees:

| Configuration | x64 output | x86 output |
| --- | --- | --- |
| Debug (default) | `builds/build-64/bin` | `builds/build/bin` |
| Release | `builds/build-prod-64/bin` | `builds/build-prod/bin` |
| RelWithDebInfo | `builds/build-release-64/bin` | `builds/build-release/bin` |

The image downloads both target toolchains, including the x64-hosted x86
cross-compiler and x86 libraries.
The runner selects `vcvarsall.bat amd64_x86` for a 32-bit target; Windows CMake
and vcpkg helper tools still run as x64 host tools. Target dependencies use the
`x86-windows-mh` triplet and a separate installed tree. Rebuild an older image
with `scripts/build_windows_container.sh image`: the extra target download
supplies both the x86 compiler and the standard `vcvarsamd64_x86.bat` entry point
that vcpkg uses to discover x86 support.

Use a dedicated checkout for Wine builds. CMake caches contain absolute Windows
paths (`W:/...`); do not reuse a cache configured on native Windows at another
drive/path. One build at a time is allowed per checkout to avoid sharing a Wine
prefix between concurrent compiler/PDB services.

The image pins the msvc-wine and vcpkg revisions. Windows helper downloads are
verified against the hashes in the pinned vcpkg metadata. The selected VS 2022
package manifest is retained in `/opt/msvc-wine`. When updating the framework's
vcpkg baseline, update the Dockerfile's `VCPKG_REVISION` and rebuild the image.
The runner rejects a cached vcpkg checkout belonging to a different image revision.

The runner prepares the pinned vcpkg port-version cache using Linux filesystem
operations. This works around the Windows vcpkg release passing extended paths
with forward slashes to Wine during port extraction. The cached port contents
come from the same pinned image checkout; dependency compilation still uses the
Windows tools. PowerShell runs in a Wine console under Xvfb because it cannot
initialize its console host from Wine's plain redirected command prompt.

## Validation

Validated on x86-64 Linux with rootless Podman, Wine 11.0 and MSVC 19.44:

- A cold install of all 20 packages in the Windows dependency graph, including
  OpenSSL, DirectXTK, ImGui and Sentry/Crashpad.
- A complete Debug x64 build of the default targets: a local project's launcher
  EXE, client DLL, server EXE and test executable, plus `FrameworkTests.exe`.
  This includes MASM code, the CEF wrapper and `cef_subprocess.exe`.
- The compiler/linker/runtime check, including Windows structured exceptions.
- `RunFrameworkTests`: 460 tests passed across 34 modules under Wine.
- Nonzero Windows exit codes reaching Linux, and rejection of concurrent builds
  sharing the same Wine prefix.

These are build checks. They do not establish that the launcher can map the game
image, install its hooks or render correctly under GE-Proton or native Windows;
that requires a separate game-runtime test.

The server runner has not yet been validated through a successful server startup.
Its first runtime attempt stalled during Wine prefix initialization; the runner
now disables the Mono/HTML installers during that step. It was stopped before
the server started, and the revised initialization has not been run again.
