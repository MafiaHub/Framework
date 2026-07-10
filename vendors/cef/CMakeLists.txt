# CEF (Chromium Embedded Framework) vendor integration
#
# Automatically downloads the CEF minimal binary distribution at configure time
# for the target architecture (32-bit or 64-bit Windows).
#
# Set CEF_VERSION to pin a specific CEF release. The version string comes from
# https://cef-builds.spotifycdn.com/index.html (pick a stable build).
#
# Provides:
#   CEF           - INTERFACE library target (link against this)
#   CEFCopyDlls   - Custom target that copies runtime DLLs and resources to bin/

# -------------------------------------------------------------------
# CEF version - update this to upgrade CEF
# -------------------------------------------------------------------
set(CEF_VERSION "150.0.11+gb887805+chromium-150.0.7871.115" CACHE STRING "CEF binary distribution version" FORCE)

# -------------------------------------------------------------------
# Platform detection
# -------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CEF_PLATFORM "windows64")
else()
    set(CEF_PLATFORM "windows32")
endif()

message(STATUS "CEF: Target platform is ${CEF_PLATFORM}")

# -------------------------------------------------------------------
# Download CEF binary distribution (configure-time, idempotent)
# -------------------------------------------------------------------
include(DownloadCEF)
DownloadCEF("${CEF_PLATFORM}" "${CEF_VERSION}" "${CMAKE_CURRENT_SOURCE_DIR}")

# CEF_ROOT is now set by DownloadCEF() as a CACHE INTERNAL variable
# pointing to the extracted distribution directory.

# -------------------------------------------------------------------
# Build the CEF C++ wrapper (libcef_dll_wrapper)
# -------------------------------------------------------------------
file(GLOB_RECURSE CEF_WRAPPER_SOURCES "${CEF_ROOT}/libcef_dll/*.cc")
add_library(libcef_dll_wrapper STATIC ${CEF_WRAPPER_SOURCES})
target_include_directories(libcef_dll_wrapper PUBLIC "${CEF_ROOT}")
target_compile_definitions(libcef_dll_wrapper PRIVATE
    WRAPPING_CEF_SHARED
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(libcef_dll_wrapper PRIVATE /W0)

# -------------------------------------------------------------------
# CEF interface target
# -------------------------------------------------------------------
add_library(CEF INTERFACE)
target_include_directories(CEF INTERFACE "${CEF_ROOT}")

# Link the import library from the Release directory
target_link_directories(CEF INTERFACE "${CEF_ROOT}/Release")
target_link_libraries(CEF INTERFACE libcef_dll_wrapper libcef)
target_compile_definitions(CEF INTERFACE NOMINMAX)

# -------------------------------------------------------------------
# Copy runtime DLLs and resources to the bin directory
# -------------------------------------------------------------------
add_custom_target(CEFCopyDlls ALL)

# Glob all DLLs, .bin and .json files from Release/
file(GLOB CEF_RELEASE_FILES
    "${CEF_ROOT}/Release/*.dll"
    "${CEF_ROOT}/Release/*.bin"
    "${CEF_ROOT}/Release/*.json"
)

foreach(f ${CEF_RELEASE_FILES})
    add_custom_command(TARGET CEFCopyDlls POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${f}" "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()

# Resources: icudtl.dat, *.pak, locales/
if(IS_DIRECTORY "${CEF_ROOT}/Resources")
    add_custom_command(TARGET CEFCopyDlls POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_ROOT}/Resources/icudtl.dat" "${CMAKE_BINARY_DIR}/bin"
    )

    file(GLOB CEF_PAK_FILES "${CEF_ROOT}/Resources/*.pak")
    foreach(pak ${CEF_PAK_FILES})
        add_custom_command(TARGET CEFCopyDlls POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${pak}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()

    if(IS_DIRECTORY "${CEF_ROOT}/Resources/locales")
        add_custom_command(TARGET CEFCopyDlls POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_ROOT}/Resources/locales" "${CMAKE_BINARY_DIR}/bin/locales"
        )
    endif()
endif()
