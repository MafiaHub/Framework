# Hello-plugin: standalone sample. Builds against the Framework Plugin SDK
# headers only — no link dependency on the Framework itself. The output
# .dll/.so is loaded at runtime by the Framework server's plugin loader.
#
# Standalone build:
#   cmake -S . -B build
#   cmake --build build
#
# The result lives in build/ as hello-plugin.dll (Windows) or
# libhello-plugin.so (Linux/macOS). Drop it next to its manifest in the
# server's modules directory.

cmake_minimum_required(VERSION 3.20)
project(hello-plugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(hello-plugin SHARED plugin.cpp)

# Resolve the SDK headers. When built from the in-tree location, the path
# is relative; when extracted as a template for an out-of-tree plugin,
# override with -DFW_PLUGIN_SDK_INCLUDE_DIR=...
set(FW_PLUGIN_SDK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk"
    CACHE PATH "Path to fw_plugin_abi.h and fw_plugin.hpp")
target_include_directories(hello-plugin PRIVATE "${FW_PLUGIN_SDK_INCLUDE_DIR}")

# Strip the lib prefix on Unix so the on-disk name matches the manifest
# "entry" field across platforms.
set_target_properties(hello-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hello-plugin"
)
