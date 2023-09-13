set(BUILD_SHARED_LIBS CACHE BOOL OFF)
set(BUILD_SAMPLES CACHE BOOL OFF)

# Add OpenSSL
if (WIN32)
    set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/openssl")
else ()
    OPTION(ENABLE_OPENSSL_FROM_SOURCE "BUILD OPENSSL FROM SOURCE AT CONFIGURE TIME" ON)

    if (ENABLE_OPENSSL_FROM_SOURCE)
        set(BUILD_OPENSSL_WORKING_DIR ${CMAKE_BINARY_DIR}/_external/build-openssl)
        set(BUILD_OPENSSL_SRC_DIR ${CMAKE_SOURCE_DIR}/vendors/openssl)
        set(BUILD_OPENSSL_INSTALL_DIR "${BUILD_OPENSSL_WORKING_DIR}/install/OpenSSL")
        file(MAKE_DIRECTORY ${BUILD_OPENSSL_WORKING_DIR})
        if (NOT EXISTS ${BUILD_OPENSSL_INSTALL_DIR})
            message(STATUS "Building OpenSSL.. at ${BUILD_OPENSSL_WORKING_DIR}, Install DIR ${BUILD_OPENSSL_INSTALL_DIR}")
            execute_process(
                    COMMAND ${CMAKE_COMMAND} ${BUILD_OPENSSL_SRC_DIR} -DINSTALL_DIR=${BUILD_OPENSSL_INSTALL_DIR}
                    WORKING_DIRECTORY ${BUILD_OPENSSL_WORKING_DIR}
            )
            execute_process(
                    COMMAND ${CMAKE_COMMAND} --build .
                    WORKING_DIRECTORY ${BUILD_OPENSSL_WORKING_DIR}
            )
        else ()
            message(STATUS "${BUILD_OPENSSL_INSTALL_DIR} already exists, skips rebuilding..")
        endif ()

        set(OPENSSL_ROOT_DIR "${BUILD_OPENSSL_INSTALL_DIR}" CACHE INTERNAL "OPENSSL ROOT DIR")
        message(MESSAGE "Setting OPENSSL ROOT DIR to newly built OpenSSL ${OPENSSL_ROOT_DIR}")
    endif ()
endif ()


# Build mongoose
add_subdirectory(mongoose)

# Build semver
add_subdirectory(semver)

# Build SPDLOG
set(SPDLOG_BUILD_SHARED OFF)
add_subdirectory(spdlog)

# Build JSON and GLM
add_subdirectory(json)
add_subdirectory(glm)

# Build CPPFS
set(OPTION_BUILD_TESTS CACHE BOOL OFF)
add_subdirectory(cppfs)

# Build HTTPLib
set(OPENSSL_USE_STATIC_LIBS ON)
set(HTTPLIB_REQUIRE_OPENSSL ON)
set(HTTPLIB_COMPILE ON)
add_subdirectory(httplib)

# Build slikenet
add_subdirectory(slikenet)

# Build firebase
add_subdirectory(firebase)

# Build sentry
set(CURL_STATICLIB ON)
add_subdirectory(sentry)

# Build curl
add_subdirectory(curl)

# Build Optick
set(OPTICK_BUILD_SHARED CACHE BOOL "" OFF)
set(OPTICK_INSTALL_TARGETS CACHE BOOL "" OFF)
add_subdirectory(optick)

# Build NodeJS
set(ENABLE_LIB_ONLY CACHE BOOL "" ON)
add_subdirectory(nodejs)

# Build flecs
set(FLECS_SHARED_LIBS CACHE BOOL "" OFF)
add_subdirectory(flecs)

# Build FMT
add_subdirectory(fmt)

# Build libsig
add_subdirectory(libsig)

# Build sfd
add_subdirectory(sfd)

# Build OS specific libraries
if (WIN32)
    # Build other win32 deps
    add_subdirectory(minhook)
    add_subdirectory(steamworks)
    add_subdirectory(galaxy)
    add_subdirectory(udis86)
    add_subdirectory(ntdll)
    add_subdirectory(imgui)
    add_subdirectory(sdl2)
    add_subdirectory(discord)
    add_subdirectory(directxtk)
    add_subdirectory(cef)

    # Lua
    add_subdirectory(lua-5.2.1)
    add_subdirectory(lua-5.3.6)
    add_subdirectory(lua-5.4.4)

    set(StackWalker_DISABLE_TESTS CACHE BOOL "" ON)
    add_subdirectory(StackWalker)
endif ()

# Build v8pp
set(V8PP_ISOLATE_DATA_SLOT 1 CACHE STRING "v8::Isolate data slot number, used in v8pp for shared data")
set(V8PP_HEADER_ONLY 1 CACHE BOOL "Header-only library")
set(V8_COMPRESS_POINTERS 0 CACHE BOOL "Use new V8 ABI with V8_COMPRESS_POINTERS and V8_31BIT_SMIS_ON_64BIT_ARCH")
set(V8_NUGET_DEV_DIR "v8-")
set(V8_NUGET_BIN_DIR "v8-")
set(BUILD_TESTING OFF)
list(APPEND CMAKE_MODULE_PATH "v8pp/cmake")
add_subdirectory(v8pp)
