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

# MafiaNet (networking) - fetched, not vendored.
#
# MafiaNet owns RakVoice, and therefore owns RakVoice's Opus and RNNoise
# dependencies, which its own build fetches. Vendoring MafiaNet here would mean
# hoisting those two a level up into this repository: ~85 MB of third-party
# source, a collision with this repo's `vendors/**/*.cmake` ignore rule, and a
# re-vendor split across three trees that have to stay in lockstep. Fetching the
# release instead keeps the dependency boundary where it belongs.
#
# This is the only FetchContent in the Framework build; everything else under
# vendors/ is genuinely vendored in-tree. Building therefore needs network access
# on a cold configure (the fetch is cached in the build tree afterwards).
include(FetchContent)

# Framework links the static library only; samples and MafiaNet's own test suite
# are not our concern.
set(MAFIANET_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MAFIANET_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MAFIANET_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(MAFIANET_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Pinned to a release tag, never a branch: the message-id enum is positional, so
# an unpinned bump would silently change the wire format. The pin lives in
# cmake/MafiaNetPin.cmake so the release tooling can watch that one path.
include(MafiaNetPin)

# No GIT_SHALLOW: it makes CMake clone the default branch at depth 1 and then check the
# pin out of it, which only resolves while the pinned commit is still that branch's tip.
# Any later MafiaNet commit leaves the pin unreachable and a cold configure dies with
# "unable to read tree". A full clone is the cost of pinning by commit at all.
FetchContent_Declare(
    MafiaNet
    GIT_REPOSITORY https://github.com/MafiaHub/MafiaNet.git
    GIT_TAG        ${MAFIANET_PIN}
)
FetchContent_MakeAvailable(MafiaNet)

# Reconcile the fetched dependencies with this build's CRT convention.
#
# Framework uses the RELEASE CRT even in Debug builds: FrameworkSetup forces
# CMAKE_MSVC_RUNTIME_LIBRARY to MultiThreadedDLL and replaces
# CMAKE_CXX_FLAGS_DEBUG outright, so no /MDd and no _DEBUG reach its objects.
#
# MafiaNet follows the ordinary convention instead -- Source/CMakeLists.txt does
# target_compile_definitions(... $<$<CONFIG:Debug>:_DEBUG>). Defining _DEBUG makes
# MSVC select the debug CRT semantics in the object itself: _ITERATOR_DEBUG_LEVEL
# becomes 2 and the object records MDd_DynamicDebug. Linking that against
# Framework's MD_DynamicRelease objects fails with LNK2038, and no flag or
# MSVC_RUNTIME_LIBRARY property can override it, because the definition is what
# drives it. CI confirmed exactly that: the property was applied correctly and the
# objects still came out MDd.
#
# So drop the definition on the fetched targets rather than fighting the symptom.
# This is Framework adapting to its own unusual choice, not a defect upstream --
# MafiaNet is right to define _DEBUG when it is built normally.
#
# Nothing analogous was needed while MafiaNet was vendored: that copy was a bare
# add_library() with no target_compile_definitions of its own.
if(MSVC)
    foreach(_mafianet_target MafiaNetStatic MafiaNet opus rnnoise)
        if(TARGET ${_mafianet_target})
            set_property(TARGET ${_mafianet_target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

            get_target_property(_fw_defs ${_mafianet_target} COMPILE_DEFINITIONS)
            if(_fw_defs)
                list(REMOVE_ITEM _fw_defs "$<$<CONFIG:Debug>:_DEBUG>" "_DEBUG")
                set_property(TARGET ${_mafianet_target} PROPERTY
                    COMPILE_DEFINITIONS ${_fw_defs})
            endif()
        endif()
    endforeach()
endif()

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

# PhysicsFS 3.2.0 and miniz 3.0.2 both declare cmake_minimum_required(VERSION 3.0), which CMake 4
# refuses outright. Raise the policy floor for just these two rather than patching vendored files,
# so updating them stays a plain directory replace.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build PhysicsFS (zlib license) - the virtual file system backing client resource packages.
# Only the ZIP archiver is kept: resource packages are ZIP payloads, and every other format is
# dead weight (and extra parsers exposed to server-supplied bytes).
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

# Build miniz (MIT) - ZIP writing for the server-side resource packager. PhysicsFS bundles its
# own miniz for reading only, and it is private to that library.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
add_subdirectory(miniz)

unset(CMAKE_POLICY_VERSION_MINIMUM)

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
