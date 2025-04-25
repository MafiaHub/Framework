# Ultralight SDK

Ultralight is a lightweight, universal web renderer for C++.

Please visit our [website](https://ultralig.ht) for licensing information.

## Prerequisites

Before building the Ultralight SDK, ensure you have the following:

 * CMake (version 3.5 or later)
 * A C++17 compatible compiler

### Windows-specific Requirements

If building on Windows without VS 2022, you'll also need:

 * VS 2022 Redistributable: https://aka.ms/vs/17/release/vc_redist.x64.exe

## Building Samples and Tools

The SDK includes a number of samples and tools that can be built using CMake.

### Building via CMake

Run the following command in the root of the Ultralight SDK directory to build using the default generator:

```
cmake -B build && cmake --build build --config Release && cmake --install build --config Release
```

Build output can be found in the `build/out` directory.

### Building with Visual Studio Code

The SDK includes Visual Studio Code integration for convenient development:

1. Open the SDK folder in VS Code
2. Install the recommended extensions when prompted (C/C++ and CMake Tools)
3. Select your preferred build preset (Release or Debug) when prompted
4. Build the project using one of these methods:
   - Click the "Build" button in the CMake status bar
   - Run the "Build (Release)" or "Build (Debug)" task from the Terminal menu
   - Press F7 to build with the default configuration

#### Running Samples in VS Code

After building, you can run and debug any sample:
- Open the "Run and Debug" view (Ctrl+Shift+D)
- Select the sample you want to run from the dropdown
- Press F5 to start debugging or Ctrl+F5 to run without debugging

Each sample is pre-configured with the correct launch settings for Windows, macOS, and Linux.

## Using the Library

For information using the library in your application, please visit our [online docs](https://docs.ultralig.ht).

## Useful Links

| Link                       | URL                                   |
| -------------------------- | ------------------------------------- |
| __Website__                | <https://ultralig.ht>                 |
| __Join our Discord!__      | <https://chat.ultralig.ht>            |
| __Documentation__          | <https://docs.ultralig.ht>            | 