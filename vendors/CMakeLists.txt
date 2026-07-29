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

FetchContent_Declare(
    MafiaNet
    GIT_REPOSITORY https://github.com/MafiaHub/MafiaNet.git
    GIT_TAG        ${MAFIANET_PIN}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(MafiaNet)

# Pin the fetched dependencies to this build's MSVC runtime.
#
# FrameworkSetup forces the release CRT (MultiThreadedDLL) even in Debug builds,
# and replaces CMAKE_CXX_FLAGS_DEBUG outright so no /MDd survives. MafiaNet, Opus
# and RNNoise are each their own CMake project with their own project() call,
# which re-initialises compiler flags in that scope from the cache and so falls
# back to MultiThreadedDebugDLL under a Debug configuration. Linking a /MDd static
# library into /MD objects fails with LNK2038 on _ITERATOR_DEBUG_LEVEL and
# RuntimeLibrary.
#
# This did not arise while MafiaNet was vendored: that copy was a bare
# add_library() with no project() of its own, so it simply inherited this scope.
#
# The value is spelled out rather than read from CMAKE_MSVC_RUNTIME_LIBRARY so it
# cannot silently become empty across the subproject boundary -- an empty property
# means "use the default", which is exactly the MDd we are trying to avoid. Keep it
# in step with cmake/FrameworkSetup.cmake.
if(MSVC)
    set(_fw_msvc_runtime "MultiThreadedDLL")

    message(STATUS "MafiaNet CRT pin: parent CMAKE_MSVC_RUNTIME_LIBRARY='${CMAKE_MSVC_RUNTIME_LIBRARY}', forcing '${_fw_msvc_runtime}'")
    message(STATUS "MafiaNet CRT pin: CMAKE_CXX_FLAGS_DEBUG='${CMAKE_CXX_FLAGS_DEBUG}'")

    foreach(_mafianet_target MafiaNetStatic MafiaNet opus rnnoise)
        if(TARGET ${_mafianet_target})
            set_property(TARGET ${_mafianet_target} PROPERTY
                MSVC_RUNTIME_LIBRARY "${_fw_msvc_runtime}")
            get_target_property(_fw_applied ${_mafianet_target} MSVC_RUNTIME_LIBRARY)
            message(STATUS "MafiaNet CRT pin: ${_mafianet_target} -> ${_fw_applied}")
        else()
            message(STATUS "MafiaNet CRT pin: target ${_mafianet_target} NOT FOUND")
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

# Build lua
add_subdirectory(lua-5.4.4)

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
