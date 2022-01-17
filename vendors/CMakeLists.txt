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

# Build Discord SDK
add_subdirectory(discord)

# Build LibCEF
add_subdirectory(cef)

# Build slikenet
add_subdirectory(slikenet)

# Build Ultralight
add_subdirectory(ultralight)

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

# Add freetype
OPTION(ENABLE_FREETYPE_FROM_SOURCE "BUILD FREETYPE FROM SOURCE AT CONFIGURE TIME" ON)

if (ENABLE_FREETYPE_FROM_SOURCE)
    set(BUILD_FREETYPE_WORKING_DIR ${CMAKE_BINARY_DIR}/_external/build-freetype)
    set(BUILD_FREETYPE_SRC_DIR ${CMAKE_SOURCE_DIR}/vendors/freetype)
    set(BUILD_FREETYPE_INSTALL_DIR "${BUILD_FREETYPE_WORKING_DIR}/install")
    file(MAKE_DIRECTORY ${BUILD_FREETYPE_WORKING_DIR})
    if (NOT EXISTS ${BUILD_FREETYPE_INSTALL_DIR})
        message(STATUS "Building freetype.. at ${BUILD_FREETYPE_WORKING_DIR}, Install DIR ${BUILD_FREETYPE_INSTALL_DIR}")
        execute_process(
                COMMAND ${CMAKE_COMMAND} ${BUILD_FREETYPE_SRC_DIR} -DINSTALL_DIR=${BUILD_FREETYPE_INSTALL_DIR}
                WORKING_DIRECTORY ${BUILD_FREETYPE_WORKING_DIR}
        )
        execute_process(
                COMMAND ${CMAKE_COMMAND} --build .
                WORKING_DIRECTORY ${BUILD_FREETYPE_WORKING_DIR}
        )
        execute_process(
                COMMAND ${CMAKE_COMMAND} --build . --target install
                WORKING_DIRECTORY ${BUILD_FREETYPE_WORKING_DIR}
        )
    else ()
        message(STATUS "${BUILD_FREETYPE_INSTALL_DIR} already exists, skips rebuilding..")
    endif ()

    set(FREETYPE_DIR freetype CACHE INTERNAL "FREETYPE ROOT DIR")
    set(FREETYPE_LIBRARY "${BUILD_FREETYPE_INSTALL_DIR}/freetype.lib" CACHE INTERNAL "FREETYPE LIBRARY")
    set(FREETYPE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendors/freetype/include;${BUILD_FREETYPE_INSTALL_DIR}/freetype2" CACHE INTERNAL "FREETYPE INCLUDE DIRS")
    include_directories("${BUILD_FREETYPE_INSTALL_DIR}/freetype2")
    link_directories(${BUILD_FREETYPE_INSTALL_DIR})
    message("Setting FREETYPE ROOT DIR to newly built freetype ${BUILD_FREETYPE_INSTALL_DIR}")
endif ()

# Build OS specific libraries
if (WIN32)
    add_subdirectory(minhook)
    add_subdirectory(steamworks)
    add_subdirectory(galaxy)
    add_subdirectory(udis86)
    add_subdirectory(ntdll)
    add_subdirectory(imgui)
    add_subdirectory(sdl2)
    add_subdirectory(rmlui)
endif ()

