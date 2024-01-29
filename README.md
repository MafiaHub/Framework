<div align="center">
   <a href="https://github.com/MafiaHub/Framework"><img src="https://github.com/MafiaHub/Framework/assets/9026786/43e839f2-f207-47bf-aa59-72371e8403ba" alt="MafiaHub" /></a>
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

This codebase provides a suite of tools and libraries to simplify the development of multi-player modifications and ensure consistency across all of them. The primary goal is to provide a common foundation and interface with shared functionality and data. It covers many fields we found necessary during the development of multi-player mods, such as:
* **Networking**: The core of the framework provides all the necessary tools to synchronize data between players.
* **ECS**: Backed by a robust ECS framework that simplifies entity management and world streaming, it is also easily extensible.
* **Scripting**: The **Node.js** scripting layer provides an easy way to create and manage game modes used on game servers.
* **Logging**: It is always vital to log actions and errors, so the framework provides a simple way.
* **GUI**: It provides a simple way to create and manage GUI elements using the **Ultralight** library.
* **Sentry**: The framework provides a simple way to report errors and exceptions to the **Sentry** service.
* **Firebase**: It easily stores and retrieves data from the **Firebase** service. Including stats, player data, and more.
* **Externals**: Contains wrappers for various libraries and services used within the framework.
* **Integrations**: Provides a simple server and client moddable setup combining various framework components, allowing you to focus on game-specific features.
* **Utils**: It provides useful functions and classes throughout the framework.

**MafiaHub Services** are NOT part of this project, but our framework provides a simple way to integrate them. Feel free to ask us for more information about this service so we can provide the resources and a license to use it.

## Contributing

We're always looking for new contributors, so if you have any ideas or suggestions, please let us know, and we'll see how we can improve it. You can reach us at our Discord server [MafiaHub](https://discord.gg/c6gW9yRXZH) or raise an issue on our repository.

If you're interested in development, please read our [Contribution Guidelines](.github/CONTRIBUTING.md).

## Building

We use **CMake** to build our projects so that you can use any of the supported build systems. We currently support **Windows**, **Linux**, and **MacOS** operating systems. You can follow this guide to get started:

First make sure your Git client supports LFS objects, visit [Git LFS page](https://git-lfs.github.com/) for more info.

```sh
# Clone the repo
git clone https://github.com/MafiaHub/Framework.git
cd Framework
```
**Note:** If you have issues cloning the repository (Git LFS-related errors), first ensure you have Git LFS support enabled. If you do, and this looks to be a server issue, please get in touch with [@ZaKlaus](https://github.com/zpl-zak) on our [Discord](https://discord.gg/eBQ4QHX) server to investigate it.

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

#### Visual Studio 2022 support

Please ensure you have the cmake tools installed in your copy of Visual Studio first.
Open your newly cloned Framework repository folder in Visual Studio, and it will automatically set up everything for you!

Things to note:
- `code/projects` folder is hidden by default as it's ignored by Git, in Solution Explorer, enable **Show All Files** option to see your project files.
- Changes in your project's cmake will not be auto-detected; cmake will only reload config on the build. Otherwise, you can do it yourself in the Projects section in the main menu.

#### CLion support

The guide on how to set up the project files for CLion is available [here](.github/CLION_GUIDE.md).

## Add a multi-player project to the framework

Multi-player modifications are cloned into the `code/projects` directory and are automatically picked up by the framework. We use this approach to efficiently manage the projects and their dependencies and perform mass changes and general maintenance during development.

```sh
# Create and navigate to the folder
mkdir -p code/projects
cd code/projects

# Clone an MP repo that uses the framework
git clone git@github.com:<your-awesome-username>/<your-amazing-project>.git

# e.g.
git clone https://github.com/MafiaHub/MafiaMP.git
```

Now, you can access your targets and build them within the framework.

To exclude a project from compilation, create an empty file called `IGNORE` in the project's root.

## License

The code is licensed under the [MafiaHub OSS](LICENSE.txt) license.

The 5th clause ensures the work can focus primarily on this repository, as we provide access to the framework and its services. This is important to ensure that the framework is not used for other purposes, such as creating other projects, that would diverge from the framework. This approach guarantees that the changes are directly made to the framework, having a healthy ecosystem in mind.
