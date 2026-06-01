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

# Build MafiaNet (networking, vendored fork of RakNet/SLikeNet)
add_subdirectory(mafianet)

# Build sentry
set(CURL_STATICLIB ON)
add_subdirectory(sentry)

# Build curl
add_subdirectory(curl)

# Build flecs
set(FLECS_SHARED_LIBS CACHE BOOL "" OFF)
add_subdirectory(flecs)

# Build FMT
add_subdirectory(fmt)

# Build libsig
add_subdirectory(libsig)

# Build sfd
add_subdirectory(sfd)

# Build lua
add_subdirectory(lua-5.4.4)

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
