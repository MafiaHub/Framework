cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

# Must precede project(): the toolchain file is read as part of it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/VcpkgBootstrap.cmake)

project(Framework CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

# fw_set_binary_identity(): version info, icon and manifest identity for shipped binaries
include(FrameworkBinaryIdentity)
# fw_stage_runtime(): runtime files next to a client or server
include(FrameworkRuntime)
# install()/export rules producing the prebuilt SDK
include(FrameworkInstall)

# As a subdirectory (a project built with -DFW_SOURCE_DIR, see cmake/FrameworkBootstrap.cmake) the
# project owns the flags and the Framework contributes its targets only.
if(PROJECT_IS_TOP_LEVEL)
    include(FrameworkSetup)
    # compile_commands.json for clangd and the agents that query it (AGENTS.md, Code Intelligence).
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
# Scripts a project runs from its own build (pattern table checks); the SDK ships the same ones.
set(Framework_SCRIPTS_DIR "${PROJECT_SOURCE_DIR}/scripts" CACHE INTERNAL "Framework helper scripts")

# Profiling (Tracy): instrumentation compiles out entirely when OFF.
# ON by default with on-demand mode - zero overhead until a profiler connects.
option(FW_PROFILING "Enable Tracy profiler instrumentation" ON)

# DLL injection launch path (VirtualAllocEx + WriteProcessMemory + CreateRemoteThread).
# OFF by default: every project PE-loads its game, and those imports alone get the
# launcher flagged as a trojan by Windows Defender.
option(FW_DLL_INJECTION "Build the launcher's DLL injection launch path" OFF)

# Sanitizer build, for the tests: empty or "thread" (ThreadSanitizer, Linux and macOS). Set before
# the vendors so those built from source are instrumented too. What comes prebuilt is not: the vcpkg
# packages, libnode, and MafiaNet unless FW_MAFIANET_ROOT names a prefix built with
# -DMAFIANET_SANITIZER=thread. Never install or release a tree configured with it.
set(FW_SANITIZE "" CACHE STRING "Sanitizer to build with: empty or 'thread'")
set_property(CACHE FW_SANITIZE PROPERTY STRINGS "" "thread")
if(FW_SANITIZE)
    if(NOT FW_SANITIZE STREQUAL "thread")
        message(FATAL_ERROR "FW_SANITIZE=${FW_SANITIZE} is not supported; leave it empty or set 'thread'")
    endif()
    if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$" OR MSVC)
        message(FATAL_ERROR "FW_SANITIZE=thread is supported on Linux and macOS only")
    endif()
    message(STATUS "Framework: building with -fsanitize=thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/vendors)

# Include directories are now managed per-target in code/framework/CMakeLists.txt
# This provides cleaner dependency tracking and faster builds

# Include all Framework dependencies
add_subdirectory(${PROJECT_SOURCE_DIR}/code)

fw_sdk_finalize()
