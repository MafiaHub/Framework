# Windows builds from Linux

The Linux entry point runs Microsoft's x64 compiler under Wine in a local
Docker image. Windows CMake, Ninja and vcpkg run under Wine too, because
dependencies such as OpenSSL require Windows host tools. The compiler,
Windows SDK, Microsoft STL and runtime use the Windows ABI required by the
game and the prebuilt dependencies. This does not compile the game itself.

From the Framework root:

```sh
bash builds/build.bat KCDCLauncher 64
bash builds/build.bat KCDCServer 64
bash builds/build.bat RunKCDCTests 64
```

The launcher target also builds the client DLL. The first invocation builds
the toolchain image and downloads dependencies. Later invocations reuse the
image, vcpkg cache and the canonical `builds/build-64` tree. Outputs go to
`builds/build-64/bin`. Set `CMAKE_BUILD_PARALLEL_LEVEL` to control build jobs;
the default is eight.

Docker on x86-64 Linux must be available to the current user. The container
runs with that user's UID and GID so generated files stay user-owned. Wine
state and caches live under `builds/`; no host Wine installation is required.
The Docker image contains Microsoft tools and is for local use, not
redistribution.
Building it accepts the Visual Studio toolchain license, as specified by
the upstream `msvc-wine` installer.

The container attaches a terminal because Windows PowerShell requires one
under Wine, including when vcpkg probes its version. TypeScript and frontend
assets are built with native Linux Node before entering the Windows build.

`builds/build.bat` is a shell/batch entry point: use `bash` on Linux and run
it directly on Windows. Windows loads the installed VS developer environment.
Both paths use the canonical 64-bit Debug tree. Do not share an already
configured tree between a native Windows build and a container build.

The Wine build uses a separate vcpkg triplet with static dependencies and
the dynamic MSVC runtime (`/MD`). It uses the supplied compiler environment
instead of searching for a registered Visual Studio installation. The
project build uses embedded debug information. Application-local dependency
copying by PowerShell is disabled; runtime staging is handled by the
repository's targets, and the entry point copies the x64 Microsoft CRT DLLs
beside the output binaries.

Wine rejects the mixed separators in vcpkg's versioned-port extraction
paths. The build therefore uses the checked-out `ports/` as an overlay,
after verifying that the checkout exactly matches the manifest baseline
and its recipes are unmodified. Manifests with version overrides are
rejected, so the workaround cannot silently choose different versions.

Keep the entire runtime output together, including CEF resources, the Steam
bridge and libnode. With Steam running and signed in, launch each process
from a separate terminal at the Framework root:

```bash
bash scripts/windows/run-proton.sh server --host 127.0.0.1 --apihost 127.0.0.1
bash scripts/windows/run-proton.sh client
```

The script uses installed Proton Experimental and Steam Linux Runtime 4,
with separate prefixes and Proton logs under `builds/proton/`. Override
`STEAM_ROOT`, `PROTON_PATH` or `STEAM_RUNTIME_PATH` if installed elsewhere.
The client finds KCD II through Steam. Its game profile is in the client
prefix; normal game-generated logs and caches can still use the game folder.
Connect to `127.0.0.1:27015` for the local server.

The launcher, client DLL, server and existing KCDC tests have been built
with this workflow on Omarchy. The tests and the server's `--help` command
run under Wine 11. Proton Experimental has also started the server with its
gamemode loaded and HTTP endpoint responding, and loaded the client through
Steam into the multiplayer main menu with DX12, ImGui and CEF initialized.
Joining a session and gameplay have not yet been validated.
