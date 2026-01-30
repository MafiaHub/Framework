# libnode - Node.js as a Library
#
# Fetches prebuilt libnode from MafiaHub/MafiaNode releases.
# Includes V8 headers for client-side usage.
#
# Targets:
#   libnode - Shared library for server (full Node.js)
#   v8      - Interface library for client (V8 headers only, links to libnode)

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

set(LIBNODE_VERSION "22.22.0" CACHE STRING "libnode version")
set(LIBNODE_GITHUB_REPO "MafiaHub/MafiaNode")
set(LIBNODE_DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/libnode-${LIBNODE_VERSION}")

# Determine platform suffix
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIBNODE_PLATFORM "win-x64")
        set(LIBNODE_ARCHIVE_EXT "zip")
    else()
        set(LIBNODE_PLATFORM "win-x86")
        set(LIBNODE_ARCHIVE_EXT "zip")
    endif()
    set(LIBNODE_LIB_NAME "libnode.lib")
    set(LIBNODE_DLL_NAME "libnode.dll")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(LIBNODE_PLATFORM "darwin-arm64")
    else()
        set(LIBNODE_PLATFORM "darwin-x64")
    endif()
    set(LIBNODE_ARCHIVE_EXT "tar.gz")
    set(LIBNODE_LIB_NAME "libnode.127.dylib")
elseif(UNIX)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(LIBNODE_PLATFORM "linux-arm64")
    else()
        set(LIBNODE_PLATFORM "linux-x64")
    endif()
    set(LIBNODE_ARCHIVE_EXT "tar.gz")
    set(LIBNODE_LIB_NAME "libnode.so.127")
endif()

set(LIBNODE_ARCHIVE_NAME "libnode-v${LIBNODE_VERSION}-${LIBNODE_PLATFORM}.${LIBNODE_ARCHIVE_EXT}")
set(LIBNODE_URL "https://github.com/${LIBNODE_GITHUB_REPO}/releases/download/v${LIBNODE_VERSION}/${LIBNODE_ARCHIVE_NAME}")
set(LIBNODE_EXTRACT_DIR "${LIBNODE_DOWNLOAD_DIR}/libnode-v${LIBNODE_VERSION}-${LIBNODE_PLATFORM}")

message(STATUS "libnode: Version ${LIBNODE_VERSION}, Platform: ${LIBNODE_PLATFORM}")
message(STATUS "libnode: URL: ${LIBNODE_URL}")

# Download and extract
if(NOT EXISTS "${LIBNODE_EXTRACT_DIR}")
    message(STATUS "libnode: Downloading prebuilt binaries...")

    set(LIBNODE_ARCHIVE_PATH "${LIBNODE_DOWNLOAD_DIR}/${LIBNODE_ARCHIVE_NAME}")

    file(DOWNLOAD
        "${LIBNODE_URL}"
        "${LIBNODE_ARCHIVE_PATH}"
        SHOW_PROGRESS
        STATUS LIBNODE_DOWNLOAD_STATUS
    )

    list(GET LIBNODE_DOWNLOAD_STATUS 0 LIBNODE_DOWNLOAD_ERROR)
    if(LIBNODE_DOWNLOAD_ERROR)
        list(GET LIBNODE_DOWNLOAD_STATUS 1 LIBNODE_DOWNLOAD_ERROR_MSG)
        message(FATAL_ERROR "libnode: Failed to download: ${LIBNODE_DOWNLOAD_ERROR_MSG}")
    endif()

    message(STATUS "libnode: Extracting...")
    file(ARCHIVE_EXTRACT
        INPUT "${LIBNODE_ARCHIVE_PATH}"
        DESTINATION "${LIBNODE_DOWNLOAD_DIR}"
    )
endif()

# Set paths
set(LIBNODE_INCLUDE_DIR "${LIBNODE_EXTRACT_DIR}/include")
set(LIBNODE_LIB_DIR "${LIBNODE_EXTRACT_DIR}/lib")
set(LIBNODE_BIN_DIR "${LIBNODE_EXTRACT_DIR}/bin")

# Verify extraction
if(NOT EXISTS "${LIBNODE_INCLUDE_DIR}")
    message(FATAL_ERROR "libnode: Include directory not found: ${LIBNODE_INCLUDE_DIR}")
endif()

# Create libnode imported target (shared library for server)
add_library(libnode SHARED IMPORTED GLOBAL)

if(WIN32)
    set_target_properties(libnode PROPERTIES
        IMPORTED_IMPLIB "${LIBNODE_LIB_DIR}/${LIBNODE_LIB_NAME}"
        IMPORTED_LOCATION "${LIBNODE_BIN_DIR}/${LIBNODE_DLL_NAME}"
    )
else()
    set_target_properties(libnode PROPERTIES
        IMPORTED_LOCATION "${LIBNODE_LIB_DIR}/${LIBNODE_LIB_NAME}"
    )

    # Create symlink for linker (libnode.dylib -> libnode.127.dylib)
    if(APPLE)
        if(NOT EXISTS "${LIBNODE_LIB_DIR}/libnode.dylib")
            file(CREATE_LINK
                "${LIBNODE_LIB_NAME}"
                "${LIBNODE_LIB_DIR}/libnode.dylib"
                SYMBOLIC
            )
        endif()
    elseif(UNIX)
        if(NOT EXISTS "${LIBNODE_LIB_DIR}/libnode.so")
            file(CREATE_LINK
                "${LIBNODE_LIB_NAME}"
                "${LIBNODE_LIB_DIR}/libnode.so"
                SYMBOLIC
            )
        endif()
    endif()
endif()

target_include_directories(libnode SYSTEM INTERFACE
    "${LIBNODE_INCLUDE_DIR}"
    "${LIBNODE_INCLUDE_DIR}/node"
    "${LIBNODE_INCLUDE_DIR}/v8"
)

# libuv headers are only needed by code that directly uses libuv APIs.
# Adding them to INTERFACE causes Windows SDK errno conflicts (EINVAL/ERANGE undefined)
# because uv.h includes system headers in a problematic order.
# Targets that need libuv should add this include directory themselves.
set(LIBNODE_UV_INCLUDE_DIR "${LIBNODE_INCLUDE_DIR}/uv" CACHE PATH "libuv include directory" FORCE)

# Note: NODE_WANT_INTERNALS is needed for Node.js internal APIs but causes
# Windows SDK errno conflicts when propagated to all targets via PCH compilation.
# Targets that need Node.js internals should add this define themselves.
# NAPI_CPP_EXCEPTIONS is needed for N-API C++ exception support.
target_compile_definitions(libnode INTERFACE
    NAPI_CPP_EXCEPTIONS=1
)

target_compile_features(libnode INTERFACE cxx_std_17)

# Platform-specific dependencies
if(WIN32)
    target_link_libraries(libnode INTERFACE
        winmm
        dbghelp
        shlwapi
        ws2_32
        iphlpapi
        psapi
        userenv
    )
elseif(APPLE)
    target_link_libraries(libnode INTERFACE
        "-framework CoreFoundation"
        "-framework CoreServices"
    )
else()
    target_link_libraries(libnode INTERFACE
        pthread
        dl
    )
endif()

# Create v8 interface target (uses V8 headers from libnode)
# This allows client code to use V8 API without linking full Node.js
add_library(v8 INTERFACE)

target_include_directories(v8 SYSTEM INTERFACE
    "${LIBNODE_INCLUDE_DIR}/v8"
)

target_compile_features(v8 INTERFACE cxx_std_17)

# V8 needs to link against libnode at runtime
target_link_libraries(v8 INTERFACE libnode)

# Export paths for other modules
set(LIBNODE_INCLUDE_DIRS "${LIBNODE_INCLUDE_DIR}" CACHE PATH "libnode include directories" FORCE)
set(LIBNODE_LIBRARY_DIR "${LIBNODE_LIB_DIR}" CACHE PATH "libnode library directory" FORCE)
set(V8_INCLUDE_DIRS "${LIBNODE_INCLUDE_DIR}/v8" CACHE PATH "V8 include directories" FORCE)

# On Windows, create a custom target to copy libnode.dll to the bin directory.
# Executables that link against libnode need to depend on this target.
if(WIN32)
    set(LIBNODE_DLL_PATH "${LIBNODE_BIN_DIR}/${LIBNODE_DLL_NAME}" CACHE PATH "libnode DLL path" FORCE)

    add_custom_target(libnode_copy_dll
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBNODE_DLL_PATH}" "${CMAKE_BINARY_DIR}/bin/"
        COMMENT "Copying libnode.dll to bin directory"
    )
endif()

message(STATUS "libnode: Include dir: ${LIBNODE_INCLUDE_DIR}")
message(STATUS "libnode: Library dir: ${LIBNODE_LIB_DIR}")
