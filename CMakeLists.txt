cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(Framework CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_STANDALONE true)

# fmt 8.0.1 (vendored standalone and bundled inside spdlog) enables a `consteval`
# constructor for FMT_STRING on Clang >= 11.1 in C++20. Newer Clang (e.g. Apple
# clang 21 / LLVM 20+) correctly rejects fmt's own internal runtime format_to()
# calls that pass FMT_STRING(...), breaking the build (fmtlib/fmt#4740, #4501).
# Both fmt copies gate on `#ifndef FMT_CONSTEVAL`, so predefine it empty to use
# fmt's supported non-consteval fallback. Drop this once fmt/spdlog are upgraded.
add_compile_definitions(FMT_CONSTEVAL=)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

if(FW_STANDALONE)
    include(FrameworkSetup)
endif()

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
