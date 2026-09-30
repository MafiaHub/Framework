cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

# Must precede project(): the toolchain file is read as part of it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/VcpkgBootstrap.cmake)

project(Framework CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_STANDALONE true)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

# fw_set_binary_identity(): version info, icon and manifest identity for shipped binaries
include(FrameworkBinaryIdentity)

if(FW_STANDALONE)
    include(FrameworkSetup)
endif()

# Profiling (Tracy): instrumentation compiles out entirely when OFF.
# ON by default with on-demand mode - zero overhead until a profiler connects.
option(FW_PROFILING "Enable Tracy profiler instrumentation" ON)

# DLL injection launch path (VirtualAllocEx + WriteProcessMemory + CreateRemoteThread).
# OFF by default: every project PE-loads its game, and those imports alone get the
# launcher flagged as a trojan by Windows Defender.
option(FW_DLL_INJECTION "Build the launcher's DLL injection launch path" OFF)

if (WIN32)
    if (MSVC)
        add_compile_options(
            $<$<COMPILE_LANGUAGE:C,CXX>:/bigobj>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
        )
    endif()
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/vendors)

# Include directories are now managed per-target in code/framework/CMakeLists.txt
# This provides cleaner dependency tracking and faster builds

# Include all Framework dependencies
add_subdirectory(${PROJECT_SOURCE_DIR}/code)
