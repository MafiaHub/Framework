<div align="center">
   <img src="https://user-images.githubusercontent.com/9026786/132325309-2e8ebecf-1154-45b2-b07a-ac9c0d3f6f94.png" alt="MafiaHub Framework" />
</div>

<div align="center">
    <a href="https://discord.gg/eBQ4QHX"><img src="https://img.shields.io/discord/402098213114347520.svg" alt="Discord server" /></a>
    <a href="LICENSE.md"><img src="https://img.shields.io/badge/License-MafiaHub%20OSS-blue" alt="license" /></a>
</div>

<br />
<div align="center">
  A suite of tools and libraries to accelerate multi-player modification development.
</div>

<div align="center">
  <sub>
    Brought to you by <a href="https://github.com/Segfaultd">@Segfault</a>,
    <a href="https://github.com/zaklaus">@zaklaus</a>,
    <a href="https://github.com/DavoSK">@DavoSK</a>,
    and other <a href="https://github.com/MafiaHub/Framework/graphs/contributors">contributors</a>!
  </sub>
</div>
<hr/>

## Introduction

This codebase provides a suite of tools and libraries to simplify the development of multi-player modifications and ensure consistency across all of them. The primary goal is to provide a common foundation and interface, in regards to shared functionality and data. It covers many fields we found important during the development of multi-player mods, such as:
* **Networking**: The core of the framework, it provides all the necessary tools to synchronize data between players.
* **ECS**: Backed by a strong ECS framework that simplifies entity management and world streaming, it is also easily extensible.
* **Scripting**: The **Node.js** scripting layer provides an easy way to create and manage resources used on game servers. It also provides a common interface accross all multi-player mods, making it easy to create and manage shared resources between games.
* **Logging**: It is always important to log actions and errors, so the framework provides a simple way to do so.
* **GUI**: It also provides a simple way to create and manage GUI elements using the **Chromium Embedded Framework** library.
* **Sentry**: The framework provides a simple way to report errors and exceptions to the **Sentry** service.
* **Firebase**: It is used to easily store and retrieve data from the **Firebase** service. Including stats, player data, and more.
* **Externals**: Contains wrappers for various libraries and services used within the framework.
* **Integrations**: Provides a simple server and client moddable setup that combines various framework components together allowing you to focus on game-specific features themselves.
* **Utils**: It provides a collection of useful functions and classes that are used throughout the framework.

**MafiaHub Services** are NOT part of this project, but our framework provides a simple way to integrate them. Feel free to ask us for more information about this service, so we could provide the resources and a license to use it.

## Contributing

We're always looking for new contributors, so if you have any ideas or suggestions, please let us know and we'll see what we can do to make it better. You can either reach us at our Discord server [MafiaHub](https://discord.gg/c6gW9yRXZH), or by raising an issue on our repository.

If you're interested in development, please read our [Contribution Guidelines](.github/CONTRIBUTING.md).

## Building

We use **CMake** to build our projects, so you can use any of the supported build systems. We support **Windows**, **Linux**, and **MacOS** operating systems at the moment. You can follow this guide to get started:

First make sure your Git client supports LFS objects, visit [Git LFS page](https://git-lfs.github.com/) for more info.

```sh
# Clone the repo
git clone https://github.com/MafiaHub/Framework.git
cd Framework
```
**Note:** If you have issues cloning the repository (Git LFS related errors), first ensure you have Git LFS support enabled. If you do and this looks to be a server issue, please contact [@ZaKlaus](https://github.com/zaklaus) on our [Discord](https://discord.gg/eBQ4QHX) server to investigate it.

### Build on macOS/Linux
```
# Configure CMake project
cmake -B build

# Build framework
cmake --build build

# Run framework tests
cmake --build build --target RunFrameworkTests
```

### Build on Windows

#### Visual Studio 2019+ support

Ensure you have the cmake tools installed in your copy of Visual Studio first.
Open your newly cloned Framework repository folder in Visual Studio and it will automagically set up everything for you!

Things to note:
- `code/projects` folder is hidden by default as it's ignored by Git, in Solution Explorer, enable **Show All Files** option to see your project files.
- Changes in your project's cmake will not be auto-detected, cmake will only reload config on build, otherwise you can do it yourself in the Projects section in main menu.
- Visual Studio 2019 embeds an old and unsupported version of ClangFormat, we highly recommend you to install [LLVM tools](https://releases.llvm.org/download.html) and set up a custom ClangFormat executable in VS's `Tools > Options > Text Editor > C/C++ > Code Style > Formatting > Use custom clang-format.exe file` property.

#### CLion support

The guide on how to set up the project files for CLion is available [here](.github/CLION_GUIDE.md).

## Add a multi-player project to the framework

Multi-player modifications are cloned into the `code/projects` directory and are automatically picked up by the framework. We use this approach so that we can easily manage the projects and their dependencies, perform mass changes and general maintenance during the development.

```sh
# Create and navigate to folder
mkdir -p code/projects
cd code/projects

# Clone a MP repo that uses the framework
git clone git@github.com:<your-awesome-username>/<your-amazing-project>.git

# e.g.
git clone https://github.com/MafiaHub/MafiaMP.git
```

Now you can access your targets and build them within the framework.

To exclude a project from compilation, simply create an empty file called `IGNORE` in the root of the project.

## License

The code is licensed under the [MafiaHub OSS](LICENSE.txt) license.

The 5th clause exists to ensure that the work can focus primarily on this repository, as we provide an access to the framework and its services. This is important to ensure that the framework is not used for other purposes, such as the creation of other projects, that would diverge from the framework. This approach guarantees that the changes are directly made to the framework itself, having a healthy ecosystem in mind.
