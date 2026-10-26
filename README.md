<div align="center">
   <a href="https://github.com/MafiaHub/Framework"><img src="https://github.com/MafiaHub/Framework/assets/9026786/43e839f2-f207-47bf-aa59-72371e8403ba" alt="MafiaHub" /></a>
</div>

<div align="center">
    <a href="https://discord.gg/eBQ4QHX"><img src="https://img.shields.io/discord/402098213114347520.svg" alt="Discord server" /></a>
    <a href="LICENSE.md"><img src="https://img.shields.io/badge/License-MafiaHub%20OSS-blue" alt="license" /></a>
</div>

<br />
<div align="center">
  Opinionated suite of tools and libraries to accelerate build multi-player modifications for AAA games.
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

This codebase provides a suite of tools and libraries to simplify the development of multi-player modifications and ensure consistency across all of them. The primary goal is to provide a common foundation and interface with shared functionality and data. It covers many fields we found necessary during the development of multi-player mods, such as:
* **Networking**: The core of the framework provides all the necessary tools to synchronize data between players.
* **ECS**: Backed by a robust ECS framework that simplifies entity management and world streaming, it is also easily extensible.
* **Scripting**: The **JavaScript/TypeScript** scripting layer provides an easy way to create and manage game modes used on game servers (powered by Node.js on server, V8 on client).
* **Logging**: It is always vital to log actions and errors, so the framework provides a simple way.
* **GUI**: It provides a simple way to create and manage GUI elements using the **Ultralight** and DearImGUI libraries.
* **Sentry**: The framework provides a simple way to report errors and exceptions to the **Sentry** service.
* **Externals**: Contains wrappers for various libraries and services used within the framework.
* **Integrations**: Provides a simple server and client moddable setup combining various framework components, allowing you to focus on game-specific features.
* **Utils**: It provides useful functions and classes throughout the framework.

**MafiaHub Services** are NOT part of this project, but our framework provides a simple way to integrate them. Feel free to ask us for more information about this service so we can provide the resources and a license to use it.

## Contributing

We're always looking for new contributors, so if you have any ideas or suggestions, please let us know, and we'll see how we can improve it. You can reach us at our Discord server [MafiaHub](https://discord.gg/c6gW9yRXZH) or raise an issue on our repository.

If you're interested in development, please read our [Contribution Guidelines](.github/CONTRIBUTING.md).

## Building

We use **CMake** to build our projects so that you can use any of the supported build systems. We currently support **Windows**, **Linux**, and **MacOS** operating systems. You can follow this guide to get started:

```sh
# Clone the repo
git clone https://github.com/MafiaHub/Framework.git
cd Framework

```

### Build on macOS/Linux

On Debian/Ubuntu, install the required system development packages first:

```sh
sudo apt install build-essential cmake \
    libssl-dev zlib1g-dev libssh2-1-dev libcurl4-openssl-dev
```

Equivalents on other distributions: `openssl-devel`, `zlib-devel`, `libssh2-devel`, `libcurl-devel` (Fedora/RHEL).

```
# Configure CMake project
cmake -B build

# Build framework
cmake --build build

# Run framework tests
cmake --build build --target RunFrameworkTests
```

### Build on Windows

To build Windows targets from Linux with the actual MSVC toolchain, see the
optional [MSVC/Wine container workflow](docs/windows-container.md).

#### Visual Studio 2022 support

Please ensure you have the cmake tools installed in your copy of Visual Studio first.
Open your newly cloned Framework repository folder in Visual Studio, and it will automatically set up everything for you!

Things to note:
- Changes in your project's cmake will not be auto-detected; cmake will only reload config on the build. Otherwise, you can do it yourself in the Projects section in the main menu.

#### CLion support

The guide on how to set up the project files for CLion is available [here](.github/CLION_GUIDE.md).

## Build a multi-player project on the framework

Projects live in their own repositories and link the Framework as a prebuilt SDK attached to each release. A project carries `cmake/FrameworkBootstrap.cmake` and `cmake/FrameworkPin.cmake`, which name the release archive and its hashes; the first configure downloads it. Set `FW_SOURCE_DIR` to a Framework checkout to build the Framework from source alongside the project, or `FW_SDK_ROOT` to a locally installed SDK (`cmake --install` of a Framework build).

## License

The code is licensed under the [MafiaHub OSS](LICENSE.txt) license.

The 5th clause ensures the work can focus primarily on this repository, as we provide access to the framework and its services. This is important to ensure that the framework is not used for other purposes, such as creating other projects, that would diverge from the framework. This approach guarantees that the changes are directly made to the framework, having a healthy ecosystem in mind.
