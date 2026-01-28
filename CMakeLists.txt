cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(Framework CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_STANDALONE true)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

if(FW_STANDALONE)
    include(FrameworkSetup)
endif()

if (WIN32)
    if (MSVC)
        add_compile_options(
            /bigobj
        )
    endif()
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/vendors)

# Include directories are now managed per-target in code/framework/CMakeLists.txt
# This provides cleaner dependency tracking and faster builds

# Include all Framework dependencies
add_subdirectory(${PROJECT_SOURCE_DIR}/code)
