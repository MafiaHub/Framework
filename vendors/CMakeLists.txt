set(BUILD_SHARED_LIBS CACHE BOOL OFF)

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

# Build firebase

add_library(Firebase INTERFACE)
set(FIREBASE_LIBS firebase_analytics firebase_auth firebase_app)
if (NOT CMAKE_CL_64 AND WIN32)
    target_link_directories(Firebase INTERFACE "firebase/lib/win_32/Release")
elseif (WIN32)
    target_link_directories(Firebase INTERFACE "firebase/lib/win_64/Release")
elseif (APPLE)
    target_link_directories(Firebase INTERFACE "firebase/lib/osx")
else ()
    target_link_directories(Firebase INTERFACE "firebase/lib/linux_64")
endif ()

target_link_libraries(Firebase INTERFACE ${FIREBASE_LIBS})
target_include_directories(Firebase INTERFACE "firebase/include")

# Build sentry
set(CURL_STATICLIB ON)
add_subdirectory(sentry)

# Build curl
add_library(Curl INTERFACE)

if (WIN32)
    target_link_directories(Curl INTERFACE "curl/lib")
    target_link_libraries(Curl INTERFACE libcurl)
else ()
    target_link_libraries(Curl INTERFACE curl)
endif ()

# Build Optick
set(OPTICK_BUILD_SHARED CACHE BOOL "" OFF)
set(OPTICK_INSTALL_TARGETS CACHE BOOL "" OFF)
add_subdirectory(optick)

# Build OS specific libraries
if (WIN32)
    add_subdirectory(minhook)
    add_subdirectory(steamworks)
endif ()

