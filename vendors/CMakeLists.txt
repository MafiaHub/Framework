set(BUILD_SHARED_LIBS CACHE BOOL OFF)
set(BUILD_SAMPLES CACHE BOOL OFF)

# Recursively find all source files in the vendors directory
file(GLOB_RECURSE VENDOR_FILES "*.cpp" "*.c" "*.h" "*.hpp")

# Apply the no-warning compile flag to all files in the vendors directory
foreach(vendor_file ${VENDOR_FILES})
    set_source_files_properties(${vendor_file} PROPERTIES COMPILE_FLAGS "-w")
endforeach()

# Add OpenSSL
if (WIN32)
    set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/openssl" CACHE PATH "Path to vendored OpenSSL")
    set(OPENSSL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/openssl/include" CACHE PATH "Path to vendored OpenSSL headers" FORCE)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_OPENSSL_VENDOR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/openssl/lib/x86")
    else ()
        set(_OPENSSL_VENDOR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/openssl/lib")
    endif ()
    set(OPENSSL_SSL_LIBRARY "${_OPENSSL_VENDOR_LIB_DIR}/libssl.lib" CACHE FILEPATH "Path to vendored OpenSSL SSL library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_VENDOR_LIB_DIR}/libcrypto.lib" CACHE FILEPATH "Path to vendored OpenSSL Crypto library" FORCE)
    set(SSL_EAY_DEBUG "${_OPENSSL_VENDOR_LIB_DIR}/libssl.lib" CACHE FILEPATH "Path to vendored OpenSSL SSL debug library" FORCE)
    set(SSL_EAY_RELEASE "${_OPENSSL_VENDOR_LIB_DIR}/libssl.lib" CACHE FILEPATH "Path to vendored OpenSSL SSL release library" FORCE)
    set(LIB_EAY_DEBUG "${_OPENSSL_VENDOR_LIB_DIR}/libcrypto.lib" CACHE FILEPATH "Path to vendored OpenSSL Crypto debug library" FORCE)
    set(LIB_EAY_RELEASE "${_OPENSSL_VENDOR_LIB_DIR}/libcrypto.lib" CACHE FILEPATH "Path to vendored OpenSSL Crypto release library" FORCE)
    add_subdirectory(openssl)
endif ()

# Build semver
add_subdirectory(semver)

# Build SPDLOG
set(SPDLOG_BUILD_SHARED OFF)
add_subdirectory(spdlog)

# Build JSON and GLM
add_subdirectory(json)
add_subdirectory(glm)

# Build Hash
add_subdirectory(hash)

# Build CPPFS
set(OPTION_BUILD_TESTS CACHE BOOL OFF)
add_subdirectory(cppfs)

# Build HTTPLib
set(OPENSSL_USE_STATIC_LIBS ON)
set(HTTPLIB_REQUIRE_OPENSSL ON)
set(HTTPLIB_COMPILE ON)
add_subdirectory(httplib)

# MafiaNet (networking) - precompiled release archive, neither vendored nor
# built from source.
#
# MafiaNet's release pipeline publishes per-platform archives (linux-x86_64,
# windows-x86/x64, macos-x86_64/arm64) containing the static library, the
# bundled Opus/RNNoise static libs RakVoice needs, all public headers, and the
# MafiaNet CMake package. Downloading one archive replaces compiling ~85 MB of
# third-party source (plus MafiaNet's own Opus/RNNoise fetches) on every cold
# configure, and keeps the dependency boundary where it belongs.
#
# This is the only download in the Framework build; everything else under
# vendors/ is genuinely vendored in-tree. A cold configure therefore needs
# network access (the archive is cached in the build tree afterwards).
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

# The archives carry a single Release configuration, compiled with the /MD
# runtime on Windows -- exactly the CRT FrameworkSetup forces even in Debug
# builds -- so the old _DEBUG/CRT reconciliation the source build needed is gone
# with it. Map every consumer configuration onto that Release config; the
# variables seed MAP_IMPORTED_CONFIG_* on the imported targets find_package
# creates, and are cleared right after so no other target is affected.
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
# Resolves OpenSSL through find_dependency: on Windows that is the vendored
# OpenSSL forced above, elsewhere the system one -- either way OpenSSL 3, which
# is what the archives were compiled against.
find_package(MafiaNet ${MAFIANET_PIN_VERSION} CONFIG REQUIRED
    PATHS "${_mafianet_root}"
    NO_DEFAULT_PATH
)
unset(CMAKE_MAP_IMPORTED_CONFIG_DEBUG)
unset(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO)
unset(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL)

# find_package imports are scoped to this directory; the consumers live under
# code/ (a sibling), which could see the old add_subdirectory alias targets but
# not these. Promote everything the package exports.
foreach(_mafianet_target MafiaNet::MafiaNetStatic MafiaNet::MafiaNet MafiaNet::opus MafiaNet::rnnoise)
    if(TARGET ${_mafianet_target})
        set_target_properties(${_mafianet_target} PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
endforeach()

# Build sentry
set(CURL_STATICLIB ON)
add_subdirectory(sentry)

# Build curl
add_subdirectory(curl)

# Build FMT
add_subdirectory(fmt)

# Build libsig
add_subdirectory(libsig)

# Build sfd
add_subdirectory(sfd)

# Build Tracy (profiler client)
# TRACY_ENABLE follows FW_PROFILING; the TracyClient target is always available
# so Framework can link it unconditionally - it compiles to no-ops when disabled.
set(TRACY_ENABLE ${FW_PROFILING} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_FIBERS ON CACHE BOOL "" FORCE) # FTL job system runs on fibers
# Multiple modules link TracyClient statically on Windows (loader exe + client
# in the game DLL). Delayed init makes the profiler start on first zone use,
# so only modules that actually record data spawn a Tracy instance.
set(TRACY_DELAYED_INIT ON CACHE BOOL "" FORCE)
add_subdirectory(tracy)

# Build FTL (Fiber Tasking Library)
set(FTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FTL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTL_CPP_17 ON CACHE BOOL "" FORCE)
add_subdirectory(ftl)

# Build libnode (Node.js as library) - provides both libnode and v8 targets
# libnode: Full Node.js shared library for server-side scripting
# v8: V8 headers interface for client-side scripting (links to libnode)
add_subdirectory(libnode)

# Build v8pp (header-only V8 binding library)
add_subdirectory(v8pp)

# Build OS specific libraries
if (WIN32)
    # Build other win32 deps
    # miniaudio backs the voice capture/playback devices. Scoped to WIN32 because its only
    # consumer is FrameworkClient, which is itself Windows-only; the library is portable and
    # can move out of this block whenever a client is built for another platform.
    add_subdirectory(miniaudio)
    add_subdirectory(minhook)
    add_subdirectory(steamworks)
    add_subdirectory(udis86)
    add_subdirectory(ntdll)
    add_subdirectory(imgui)
    add_subdirectory(imguizmo)
    add_subdirectory(discord)
    add_subdirectory(directxtk)
    add_subdirectory(cef)

    set(StackWalker_DISABLE_TESTS CACHE BOOL "" ON)
    add_subdirectory(StackWalker)
endif ()
