cmake_minimum_required(VERSION 3.21)
project(cef)

# pick your version from https://cef-builds.spotifycdn.com
set(CEF_BINARY_VERSION "cef_binary_92.0.27+g274abcf+chromium-92.0.4515.159")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if (WIN32)
        set(CEF_TARGET_PLATFORM "windows64")
    elseif(CMAKE_SYSTEM MATCHES "Darwin")
        set(CEF_TARGET_PLATFORM "macosx64")
    else()
        set(CEF_TARGET_PLATFORM "linux64")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    if (WIN32)
        set(CEF_TARGET_PLATFORM "windows32")
    else()
        set(CEF_TARGET_PLATFORM "linux32")
    endif()
endif()

set(CEF_FILE_NAME "${CEF_BINARY_VERSION}_${CEF_TARGET_PLATFORM}_minimal")
set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_FILE_NAME}.tar.bz2")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
    message("Downloading ${CEF_DOWNLOAD_URL}")
    file(DOWNLOAD ${CEF_DOWNLOAD_URL} "${CMAKE_CURRENT_SOURCE_DIR}/cef.tar.bz2" SHOW_PROGRESS)

    message("Extracting ${CEF_FILE_NAME}.tar.bz2")
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_SOURCE_DIR}/cef.tar.bz2" DESTINATION ${CMAKE_CURRENT_SOURCE_DIR} VERBOSE)

    file(RENAME ${CEF_FILE_NAME} "src")
endif()

add_subdirectory(src)

set(CEF_COPY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Release")

#

add_library(CEFTarget STATIC dummy.cpp)
#add_dependencies(CEF libcef_dll_wrapper)

target_include_directories(CEFTarget PUBLIC "src")
target_link_directories(CEFTarget PUBLIC ${CEF_COPY_DIR})

if(APPLE)
    add_custom_command(TARGET CEFTarget POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_COPY_DIR}/" "${CMAKE_BINARY_DIR}/"
            )
    target_link_libraries(CEFTarget ${CEF_COPY_DIR}/libcef.dylib)
elseif(WIN32)
    add_custom_command(TARGET CEFTarget POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_COPY_DIR}/" "${CMAKE_BINARY_DIR}/bin/"
            )
    target_link_libraries(CEFTarget PUBLIC libcef)
else()
    add_custom_command(TARGET CEFTarget POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_COPY_DIR}/" "${CMAKE_BINARY_DIR}/bin/"
            )
    target_link_libraries(CEFTarget PUBLIC libcef)
endif()
