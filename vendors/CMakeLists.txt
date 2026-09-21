set(BUILD_SHARED_LIBS CACHE BOOL OFF)
set(BUILD_SAMPLES CACHE BOOL OFF)

# Silence warnings for the trees that are still vendored in-tree. vcpkg-provided packages arrive as
# prebuilt archives and never reach the compiler here.
file(GLOB VENDOR_DIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(vendor_dir ${VENDOR_DIRS})
    # vcpkg's own checkout is a tool, not a compiled dependency, and recursing it costs seconds.
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${vendor_dir}" AND NOT vendor_dir STREQUAL "vcpkg")
        file(GLOB_RECURSE VENDOR_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${vendor_dir}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/${vendor_dir}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/${vendor_dir}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/${vendor_dir}/*.hpp")
        set_source_files_properties(${VENDOR_FILES} PROPERTIES COMPILE_FLAGS "-w")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# vcpkg-provided packages (see vcpkg.json). Versions are pinned by the manifest
# baseline, not by a directory drop.
# ---------------------------------------------------------------------------

# find_package imports are scoped to this directory by default, and the consumers live under code/
# (a sibling). This is the same problem the MafiaNet block below solves with IMPORTED_GLOBAL, but
# that property cannot be set on the ALIAS targets several of these configs define.
if(CMAKE_VERSION VERSION_LESS 3.24)
    message(FATAL_ERROR "CMake 3.24 or newer is required (CMAKE_FIND_PACKAGE_TARGETS_GLOBAL)")
endif()
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

find_package(OpenSSL REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(cppfs CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(function2 CONFIG REQUIRED)
find_package(sentry CONFIG REQUIRED)

# The crashpad backend launches this helper out-of-process, so it has to sit next to the binaries.
# The vendored drop staged it from a POST_BUILD; the port puts it under tools/.
set(_sentry_crashpad "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/crashpad_handler${CMAKE_EXECUTABLE_SUFFIX}")
if(EXISTS "${_sentry_crashpad}")
    file(COPY "${_sentry_crashpad}" DESTINATION "${CMAKE_BINARY_DIR}/bin")
else()
    message(WARNING "sentry-native crashpad_handler not found at ${_sentry_crashpad}; crash reports will not be captured")
endif()

if(WIN32)
    find_package(imgui CONFIG REQUIRED)
    # The DX backends import from these system libs (the DX12 one calls CreateDXGIFactory1 /
    # D3D12SerializeRootSignature since ImGui 1.92) and the port does not declare them. On the
    # target's interface so consumers link them after imgui, which the MSVC linker requires.
    target_link_libraries(imgui::imgui INTERFACE d3d9 d3d11 d3d12 dxgi)

    find_package(directxtk CONFIG REQUIRED)
    find_package(minhook CONFIG REQUIRED)
    find_package(stackwalker CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Still vendored in-tree
# ---------------------------------------------------------------------------

# MafiaNet (networking) - precompiled release archive, neither vendored nor
# built from source.
#
# MafiaNet's release pipeline publishes per-platform archives (linux-x86_64,
# windows-x86/x64, macos-x86_64/arm64) containing the static library, the
# bundled Opus/RNNoise static libs RakVoice needs, all public headers, and the
# MafiaNet CMake package.
#
# The pin (version + per-archive SHA-256) lives in cmake/MafiaNetPin.cmake so
# the release tooling can watch that one path: the message-id enum is
# positional, so a bump can move the wire format.
include(MafiaNetPin)

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_mafianet_platform "windows-x86")
    else()
        set(_mafianet_platform "windows-x64")
    endif()
    set(_mafianet_ext "zip")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64"
       OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"))
        set(_mafianet_platform "macos-x86_64")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"
       OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64"))
        set(_mafianet_platform "macos-arm64")
    else()
        message(FATAL_ERROR "No precompiled MafiaNet archive for macOS architectures '${CMAKE_OSX_ARCHITECTURES}' (universal builds are not supported)")
    endif()
    set(_mafianet_ext "tar.gz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(_mafianet_platform "linux-x86_64")
    set(_mafianet_ext "tar.gz")
else()
    message(FATAL_ERROR "No precompiled MafiaNet archive for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(_mafianet_name "MafiaNet-${MAFIANET_PIN_VERSION}-${_mafianet_platform}")
set(_mafianet_root "${CMAKE_BINARY_DIR}/mafianet/${_mafianet_name}")

if(NOT EXISTS "${_mafianet_root}/lib/cmake/MafiaNet/MafiaNetConfig.cmake")
    set(_mafianet_archive "${CMAKE_BINARY_DIR}/mafianet/${_mafianet_name}.${_mafianet_ext}")
    message(STATUS "Downloading precompiled MafiaNet ${MAFIANET_PIN_VERSION} (${_mafianet_platform})")
    # EXPECTED_HASH makes any failure fatal: a failed download, a repointed tag,
    # or a tampered archive all stop the configure here.
    file(DOWNLOAD
        "https://github.com/MafiaHub/MafiaNet/releases/download/v${MAFIANET_PIN_VERSION}/${_mafianet_name}.${_mafianet_ext}"
        "${_mafianet_archive}"
        EXPECTED_HASH SHA256=${MAFIANET_PIN_SHA256_${_mafianet_platform}}
        SHOW_PROGRESS
    )
    file(ARCHIVE_EXTRACT INPUT "${_mafianet_archive}" DESTINATION "${CMAKE_BINARY_DIR}/mafianet")
    file(REMOVE "${_mafianet_archive}")
endif()

# Resolves OpenSSL through find_dependency, which now lands on the vcpkg OpenSSL 3 found above --
# the same major the archives were compiled against.
find_package(MafiaNet ${MAFIANET_PIN_VERSION} CONFIG REQUIRED
    PATHS "${_mafianet_root}"
    NO_DEFAULT_PATH
)

# Two per-target fixups, on the MafiaNet targets ONLY -- deliberately not via
# the CMAKE_MAP_IMPORTED_CONFIG_* variables, which would also seed the
# OpenSSL::SSL/Crypto targets that find_dependency creates inside the same
# find_package call; those carry a single config-less IMPORTED_LOCATION, and a
# mapping they cannot satisfy fails the configure ("IMPORTED_LOCATION not set
# for ... configuration Debug").
#
#  - MAP_IMPORTED_CONFIG_*: the archives carry a single Release configuration,
#    compiled with the /MD runtime on Windows -- exactly the CRT FrameworkSetup
#    forces even in Debug builds -- so every consumer configuration maps onto it.
#  - IMPORTED_GLOBAL: find_package imports are scoped to this directory; the
#    consumers live under code/ (a sibling).
foreach(_mafianet_target MafiaNet::MafiaNetStatic MafiaNet::MafiaNet MafiaNet::opus MafiaNet::rnnoise)
    if(TARGET ${_mafianet_target})
        set_target_properties(${_mafianet_target} PROPERTIES
            IMPORTED_GLOBAL TRUE
            MAP_IMPORTED_CONFIG_DEBUG Release
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
        )
    endif()
endforeach()

# Build HTTPLib
#
# Not taken from vcpkg: that port hardcodes HTTPLIB_COMPILE=OFF, so it is header-only. The
# prebuilt MafiaHubServices archive was compiled against compiled-mode httplib and carries
# undefined references to its out-of-line symbols, which a header-only httplib cannot satisfy.
set(OPENSSL_USE_STATIC_LIBS ON)
set(HTTPLIB_REQUIRE_OPENSSL ON)
set(HTTPLIB_COMPILE ON)
add_subdirectory(httplib)

# Build semver
add_subdirectory(semver)

# Build Hash
add_subdirectory(hash)

# Build libsig
add_subdirectory(libsig)

# Build sfd
add_subdirectory(sfd)

# Build Tracy (profiler client)
#
# Not taken from vcpkg: the port exposes on-demand, fibers, verbose and crash-handler, but not
# TRACY_DELAYED_INIT. Several modules link TracyClient statically on Windows (the loader exe and
# the client inside the game DLL), and without delayed init every one of them starts a profiler
# instance at static-init time instead of on first zone use.
set(TRACY_ENABLE ${FW_PROFILING} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_FIBERS ON CACHE BOOL "" FORCE) # FTL job system runs on fibers
set(TRACY_DELAYED_INIT ON CACHE BOOL "" FORCE)
add_subdirectory(tracy)

# Build FTL (Fiber Tasking Library) - no vcpkg port exists.
set(FTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FTL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTL_CPP_17 ON CACHE BOOL "" FORCE)
add_subdirectory(ftl)

# Build libnode (Node.js as library) - provides both libnode and v8 targets
add_subdirectory(libnode)

# Build v8pp - MafiaHub fork, fetched by commit
add_subdirectory(v8pp)

# PhysicsFS 3.2.0 declares cmake_minimum_required(VERSION 3.0), which CMake 4 refuses outright.
# Raise the policy floor for just this one rather than patching vendored files, so updating it
# stays a plain directory replace.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build PhysicsFS (zlib license) - the virtual file system backing client resource packages.
#
# Not taken from vcpkg: that port passes no PHYSFS_ARCHIVE_* options, so it builds every archiver.
# Resource packages are ZIP payloads and the bytes arrive from the server, so every other format is
# an extra parser on untrusted input.
set(PHYSFS_ARCHIVE_ZIP ON CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_7Z OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_GRP OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_WAD OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_HOG OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_MVL OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_QPAK OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_SLB OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_ISO9660 OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_VDF OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(physfs)
# physfs FORCEs CMAKE_SKIP_RPATH ON into the cache under GCC/Clang. That leaks out of the
# subdirectory and disables rpath for every target in the build, including M2OServer's
# $ORIGIN - without which libnode.so shipped next to the binary no longer resolves.
set(CMAKE_SKIP_RPATH OFF CACHE BOOL "Skip RPATH" FORCE)

unset(CMAKE_POLICY_VERSION_MINIMUM)

# Build OS specific libraries
if (WIN32)
    # Not taken from vcpkg: that port installs miniaudio.h only, with no CMake package. The
    # implementation TU and the PUBLIC feature-trimming defines below it would stay hand-written
    # here either way, and splitting them from the header only invites version skew.
    add_subdirectory(miniaudio)
    add_subdirectory(steamworks)
    add_subdirectory(udis86)
    add_subdirectory(ntdll)
    # Not taken from vcpkg: the port is ImGuizmo 1.10, which predates Dear ImGui's ImDrawFlags
    # change to AddPolyline - the very break the vendored copy is patched for.
    add_subdirectory(imguizmo)
    add_subdirectory(discord)
    add_subdirectory(cef)
endif ()
