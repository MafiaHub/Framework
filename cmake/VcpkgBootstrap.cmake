# Acquires vcpkg and selects the toolchain, before project() so the toolchain is honoured.
#
# vcpkg is fetched rather than submoduled: this repository has no submodules, and the mods' release
# workflows check the Framework out with `submodules: false`. A pinned shallow fetch keeps a cold
# configure self-contained on Windows and on the Linux server runners alike.
#
# Set VCPKG_ROOT to use an existing checkout instead; the baseline in vcpkg.json still decides the
# port versions, so a different checkout does not change what gets built.

set(FW_VCPKG_PIN "5f96cd15fd745122cf27e0524606d6c1efc5fd07" CACHE STRING "vcpkg commit backing the vcpkg.json baseline")

# Deliberately not $ENV{VCPKG_ROOT}: vcvars64.bat exports it pointing at the Visual Studio bundle,
# which ships vcpkg.exe with no ports tree. Override with -DFW_VCPKG_ROOT=<path> instead.
if(FW_VCPKG_ROOT AND EXISTS "${FW_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(_fw_vcpkg_root "${FW_VCPKG_ROOT}")
else()
    set(_fw_vcpkg_root "${CMAKE_CURRENT_LIST_DIR}/../vendors/vcpkg")

    if(NOT EXISTS "${_fw_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
        find_package(Git REQUIRED)
        message(STATUS "Fetching vcpkg ${FW_VCPKG_PIN}")
        file(MAKE_DIRECTORY "${_fw_vcpkg_root}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" init -q WORKING_DIRECTORY "${_fw_vcpkg_root}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" fetch --depth 1 -q https://github.com/microsoft/vcpkg.git "${FW_VCPKG_PIN}" WORKING_DIRECTORY "${_fw_vcpkg_root}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" checkout -q FETCH_HEAD WORKING_DIRECTORY "${_fw_vcpkg_root}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
endif()

if(CMAKE_HOST_WIN32)
    set(_fw_vcpkg_bootstrap "${_fw_vcpkg_root}/bootstrap-vcpkg.bat")
    set(_fw_vcpkg_exe "${_fw_vcpkg_root}/vcpkg.exe")
else()
    set(_fw_vcpkg_bootstrap "${_fw_vcpkg_root}/bootstrap-vcpkg.sh")
    set(_fw_vcpkg_exe "${_fw_vcpkg_root}/vcpkg")
endif()

if(NOT EXISTS "${_fw_vcpkg_exe}")
    message(STATUS "Bootstrapping vcpkg")
    execute_process(COMMAND "${_fw_vcpkg_bootstrap}" -disableMetrics WORKING_DIRECTORY "${_fw_vcpkg_root}" COMMAND_ERROR_IS_FATAL ANY)
endif()

# The triplets pin the /MD runtime with static libraries and build release-only; see the comment in
# cmake/vcpkg-triplets/x64-windows-mh.cmake.
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(WIN32)
        if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_SIZEOF_VOID_P EQUAL 4 OR "$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "x86")
            set(VCPKG_TARGET_TRIPLET "x86-windows-mh" CACHE STRING "")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-windows-mh" CACHE STRING "")
        endif()
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64"
           OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64"))
            set(VCPKG_TARGET_TRIPLET "x64-osx-mh" CACHE STRING "")
        else()
            set(VCPKG_TARGET_TRIPLET "arm64-osx-mh" CACHE STRING "")
        endif()
    else()
        set(VCPKG_TARGET_TRIPLET "x64-linux-mh" CACHE STRING "")
    endif()
endif()

# A shared binary cache is what keeps the manifest from costing a source build of openssl, curl and
# sentry-native on every cold runner. Left to the caller: VCPKG_BINARY_SOURCES in the environment
# picks the store, and CI sets it. The default (a per-user files cache) already covers local work.

set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/vcpkg-triplets" CACHE STRING "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/vcpkg-ports")
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/vcpkg-ports" CACHE STRING "")
endif()
set(CMAKE_TOOLCHAIN_FILE "${_fw_vcpkg_root}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

message(STATUS "vcpkg: ${_fw_vcpkg_root} (${VCPKG_TARGET_TRIPLET})")
