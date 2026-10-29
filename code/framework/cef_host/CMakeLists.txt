# The 64-bit remote CEF host, built on its own by cmake/FrameworkCefHost.cmake from a client tree of
# either architecture. It compiles the Framework's CEF units (gui/cef, gui/resources, the logger)
# directly rather than linking FrameworkClient, which a 32-bit tree only has for 32 bits, and takes
# its few dependencies from its own vcpkg manifest.
#
# Inputs: FW_CEF_DOWNLOAD_DIR, CEF_VERSION, FW_VERSION and FW_CEF_HOST_OUTPUT_DIR, where cef_host.exe,
# a 64-bit cef_subprocess.exe and the 64-bit CEF runtime are staged.
cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

get_filename_component(FW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(VCPKG_TARGET_TRIPLET "x64-windows-mh" CACHE STRING "")
include("${FW_ROOT}/cmake/VcpkgBootstrap.cmake")

project(FrameworkCefHost CXX)

foreach(required FW_CEF_DOWNLOAD_DIR CEF_VERSION FW_VERSION FW_CEF_HOST_OUTPUT_DIR)
    if("${${required}}" STREQUAL "")
        message(FATAL_ERROR "FrameworkCefHost: ${required} is not set")
    endif()
endforeach()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "FrameworkCefHost must be configured with a 64-bit compiler")
endif()

list(APPEND CMAKE_MODULE_PATH "${FW_ROOT}/cmake")
include(DownloadCEF)
include(FrameworkBinaryIdentity)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The triplet's runtime; the packaged client ships the matching x64 CRT next to the host.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
add_compile_options(/MP /nologo /utf-8 /bigobj /Zc:__cplusplus /wd4100 /wd4251 /wd4275)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(function2 CONFIG REQUIRED)

DownloadCEF("windows64" "${CEF_VERSION}" "${FW_CEF_DOWNLOAD_DIR}")

file(GLOB_RECURSE CEF_WRAPPER_SOURCES "${CEF_ROOT}/libcef_dll/*.cc")
add_library(libcef_dll_wrapper STATIC ${CEF_WRAPPER_SOURCES})
target_include_directories(libcef_dll_wrapper PUBLIC "${CEF_ROOT}")
target_compile_definitions(libcef_dll_wrapper PRIVATE WRAPPING_CEF_SHARED UNICODE _UNICODE)
target_compile_options(libcef_dll_wrapper PRIVATE /W0)
target_link_libraries(libcef_dll_wrapper PUBLIC "${CEF_ROOT}/Release/libcef.lib")

set(FW_SRC "${FW_ROOT}/code/framework/src")

# Shared with the client's in-process path: the same handlers, scheme and settings.
set(FW_CEF_HOST_CEF_SOURCES
    ${FW_SRC}/gui/cef/app.cpp
    ${FW_SRC}/gui/cef/client.cpp
    ${FW_SRC}/gui/cef/display_handler.cpp
    ${FW_SRC}/gui/cef/life_span_handler.cpp
    ${FW_SRC}/gui/cef/load_handler.cpp
    ${FW_SRC}/gui/cef/render_handler.cpp
    ${FW_SRC}/gui/cef/renderer_app.cpp
    ${FW_SRC}/gui/cef/runtime.cpp
    ${FW_SRC}/gui/resources/mime.cpp
    ${FW_SRC}/gui/resources/resource_handler.cpp
    ${FW_SRC}/gui/resources/resource_path.cpp
    ${FW_SRC}/gui/resources/resource_registry.cpp
    ${FW_SRC}/gui/resources/resource_stream.cpp
    ${FW_SRC}/gui/resources/scheme.cpp
    ${FW_SRC}/gui/sdk.cpp
    ${FW_SRC}/logging/logger.cpp
)

add_executable(cef_host WIN32
    ${FW_SRC}/gui/remote/host/main.cpp
    ${FW_SRC}/gui/remote/host/host.cpp
    ${FW_SRC}/gui/remote/host/host_view.cpp
    ${FW_SRC}/gui/remote/host/remote_provider.cpp
    ${FW_SRC}/gui/remote/channel.cpp
    ${FW_SRC}/gui/remote/frame_buffer.cpp
    ${FW_SRC}/gui/remote/protocol.cpp
    ${FW_CEF_HOST_CEF_SOURCES}
)
# Tracy's header only: with TRACY_ENABLE unset the FW_PROFILE_* macros compile to nothing.
target_include_directories(cef_host PRIVATE "${FW_SRC}" "${FW_ROOT}/vendors/tracy/public")
target_link_libraries(cef_host PRIVATE libcef_dll_wrapper spdlog::spdlog fmt::fmt function2::function2 d3d11 dxgi)
fw_set_binary_identity(cef_host PRODUCT "MafiaHub Framework" VERSION ${FW_VERSION} COMPANY "MafiaHub" DESCRIPTION "MafiaHub Framework Web Host")

# Every process of a CEF instance registers the same schemes, so the subprocess is the same three
# units the client's 32-bit one is (see code/framework/CMakeLists.txt and docs/cef_subprocess.md).
add_executable(cef_subprocess
    ${FW_SRC}/gui/cef/subprocess_main.cpp
    ${FW_SRC}/gui/cef/renderer_app.cpp
    ${FW_SRC}/gui/resources/scheme.cpp
)
target_include_directories(cef_subprocess PRIVATE "${FW_SRC}")
target_link_libraries(cef_subprocess PRIVATE libcef_dll_wrapper)
set_target_properties(cef_subprocess PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
fw_set_binary_identity(cef_subprocess PRODUCT "MafiaHub Framework" VERSION ${FW_VERSION} COMPANY "MafiaHub" DESCRIPTION "MafiaHub Framework Web Helper")

set_target_properties(cef_host cef_subprocess PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${FW_CEF_HOST_OUTPUT_DIR}")

# The 64-bit CEF runtime beside them.
file(GLOB CEF_RUNTIME_FILES "${CEF_ROOT}/Release/*.dll" "${CEF_ROOT}/Release/*.bin" "${CEF_ROOT}/Release/*.json" "${CEF_ROOT}/Resources/*.pak" "${CEF_ROOT}/Resources/icudtl.dat")
add_custom_target(CefHostRuntime ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FW_CEF_HOST_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CEF_RUNTIME_FILES} "${FW_CEF_HOST_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Resources/locales" "${FW_CEF_HOST_OUTPUT_DIR}/locales"
    COMMENT "Staging the 64-bit CEF runtime in ${FW_CEF_HOST_OUTPUT_DIR}"
    VERBATIM)
add_dependencies(cef_host CefHostRuntime)
