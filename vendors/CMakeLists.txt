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

# Build MafiaNet (networking, vendored fork of RakNet/SLikeNet)
add_subdirectory(mafianet)

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

if(FW_METRICS_BACKEND STREQUAL "prometheus")
    add_subdirectory(prometheus-cpp-lite EXCLUDE_FROM_ALL)
endif()

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
