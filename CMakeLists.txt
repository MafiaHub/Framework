cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(Framework CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_STANDALONE true)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

if(FW_STANDALONE)
    include(FrameworkSetup)
endif()

# Profiling (Tracy): instrumentation compiles out entirely when OFF.
# ON by default with on-demand mode - zero overhead until a profiler connects.
option(FW_PROFILING "Enable Tracy profiler instrumentation" ON)

set(FW_METRICS_BACKEND "prometheus" CACHE STRING "Metrics backend")
set_property(CACHE FW_METRICS_BACKEND PROPERTY STRINGS prometheus none)
string(TOLOWER "${FW_METRICS_BACKEND}" FW_METRICS_BACKEND)
if(NOT FW_METRICS_BACKEND STREQUAL "prometheus" AND NOT FW_METRICS_BACKEND STREQUAL "none")
    message(FATAL_ERROR "Unsupported FW_METRICS_BACKEND: ${FW_METRICS_BACKEND}")
endif()
message(STATUS "Metrics backend: ${FW_METRICS_BACKEND}")

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
