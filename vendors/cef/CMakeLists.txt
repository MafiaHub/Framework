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

if(NOT EXISTS "${CMAKE_BINARY_DIR}/cef_src")
    message("Downloading ${CEF_DOWNLOAD_URL}")
    file(DOWNLOAD ${CEF_DOWNLOAD_URL} "${CMAKE_BINARY_DIR}/cef.tar.bz2" SHOW_PROGRESS)

    message("Extracting ${CEF_FILE_NAME}.tar.bz2")
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/cef.tar.bz2" DESTINATION ${CMAKE_BINARY_DIR} VERBOSE)

    file(RENAME "${CMAKE_BINARY_DIR}/${CEF_FILE_NAME}" "${CMAKE_BINARY_DIR}/cef_src")
endif()

add_subdirectory("${CMAKE_BINARY_DIR}/cef_src" "${CMAKE_BINARY_DIR}/cef_build")

set(CEF_COPY_DIR "${CMAKE_BINARY_DIR}/cef_src/Release")

#

add_library(CEFTarget STATIC dummy.cpp)
#add_dependencies(CEF libcef_dll_wrapper)

target_include_directories(CEFTarget PUBLIC "${CMAKE_BINARY_DIR}/cef_src")
target_link_directories(CEFTarget PUBLIC ${CEF_COPY_DIR})
target_link_libraries(CEFTarget PUBLIC ${CEF_COPY_DIR}/libcef.lib ${CEF_COPY_DIR}/cef_sandbox.lib)

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
