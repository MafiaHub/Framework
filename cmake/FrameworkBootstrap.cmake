# Brings the MafiaHub Framework into a project's build. include() it before project(), then call
# fw_bootstrap_finish() right after project(). A project keeps its own copy of this file (and of
# FrameworkPin.cmake beside it), the way the Framework keeps VcpkgBootstrap.cmake.
#
# Where the Framework comes from, first match wins:
#   -DFW_SOURCE_DIR=<Framework checkout>   built from source as a subdirectory: the developer loop,
#                                          where a Framework header edit is one incremental build
#   -DFW_SDK_ROOT=<install tree>           an SDK installed locally (`cmake --install` of a build)
#   FrameworkPin.cmake                     the pinned release archive, downloaded once into
#                                          FW_SDK_CACHE_DIR (-D or environment; default
#                                          ${CMAKE_BINARY_DIR}/framework-sdk) and hash-checked.
#                                          One archive per OS: the Windows one carries x86 and x64.

cmake_minimum_required(VERSION 3.24)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/FrameworkPin.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/FrameworkPin.cmake")
endif()

if(FW_SOURCE_DIR)
    get_filename_component(FW_SOURCE_DIR "${FW_SOURCE_DIR}" ABSOLUTE)
    if(NOT EXISTS "${FW_SOURCE_DIR}/cmake/VcpkgBootstrap.cmake")
        message(FATAL_ERROR "FW_SOURCE_DIR '${FW_SOURCE_DIR}' is not a Framework checkout")
    endif()
    set(FW_BOOTSTRAP_MODE "source")
    set(VCPKG_MANIFEST_DIR "${FW_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "" FORCE)
    include("${FW_SOURCE_DIR}/cmake/VcpkgBootstrap.cmake")
else()
    if(FW_SDK_ROOT)
        get_filename_component(_fw_sdk_root "${FW_SDK_ROOT}" ABSOLUTE)
        if(NOT DEFINED FW_VCPKG_CHECKOUT_DIR)
            set(FW_VCPKG_CHECKOUT_DIR "${CMAKE_BINARY_DIR}/vcpkg")
        endif()
    else()
        if(NOT DEFINED FRAMEWORK_PIN_VERSION)
            message(FATAL_ERROR "No Framework to build against: pass -DFW_SDK_ROOT=<install tree>, -DFW_SOURCE_DIR=<checkout>, or add FrameworkPin.cmake next to FrameworkBootstrap.cmake")
        endif()
        if(WIN32)
            set(_fw_platform "windows")
            set(_fw_ext "zip")
        elseif(APPLE)
            message(FATAL_ERROR "No prebuilt Framework archive for macOS; use -DFW_SOURCE_DIR")
        else()
            set(_fw_platform "linux")
            set(_fw_ext "tar.gz")
        endif()
        if(NOT DEFINED FRAMEWORK_PIN_SHA256_${_fw_platform})
            message(FATAL_ERROR "FrameworkPin.cmake has no FRAMEWORK_PIN_SHA256_${_fw_platform}")
        endif()

        if(FW_SDK_CACHE_DIR)
            set(_fw_cache "${FW_SDK_CACHE_DIR}")
        elseif(NOT "$ENV{FW_SDK_CACHE_DIR}" STREQUAL "")
            set(_fw_cache "$ENV{FW_SDK_CACHE_DIR}")
        else()
            set(_fw_cache "${CMAKE_BINARY_DIR}/framework-sdk")
        endif()
        file(TO_CMAKE_PATH "${_fw_cache}" _fw_cache)
        get_filename_component(_fw_cache "${_fw_cache}" ABSOLUTE)
        if(NOT DEFINED FW_VCPKG_CHECKOUT_DIR)
            set(FW_VCPKG_CHECKOUT_DIR "${_fw_cache}/vcpkg")
        endif()

        set(_fw_name "Framework-${FRAMEWORK_PIN_VERSION}-${_fw_platform}")
        set(_fw_sdk_root "${_fw_cache}/${_fw_name}")
        # The stamp carries the archive hash: an extraction is trusted only once it finished, and
        # only for the archive the pin names now.
        set(_fw_stamp "${_fw_sdk_root}/.fw-extracted")
        set(_fw_expected "${FRAMEWORK_PIN_SHA256_${_fw_platform}}")

        # Several builds may share the cache (the 32- and 64-bit trees, concurrent CI jobs on one
        # runner). The lock covers the extraction below and the vcpkg checkout VcpkgBootstrap makes
        # in the same directory; it is released when this file finishes.
        file(MAKE_DIRECTORY "${_fw_cache}")
        file(LOCK "${_fw_cache}/.fw-sdk.lock" GUARD FILE RESULT_VARIABLE _fw_lock TIMEOUT 3600)
        if(NOT _fw_lock EQUAL 0)
            message(FATAL_ERROR "Could not lock the Framework SDK cache ${_fw_cache}: ${_fw_lock}")
        endif()

        set(_fw_have "")
        if(EXISTS "${_fw_stamp}")
            file(READ "${_fw_stamp}" _fw_have)
            string(STRIP "${_fw_have}" _fw_have)
        endif()
        if(NOT _fw_have STREQUAL _fw_expected)
            set(_fw_archive "${_fw_cache}/${_fw_name}.${_fw_ext}")
            message(STATUS "Downloading Framework SDK ${FRAMEWORK_PIN_VERSION} (${_fw_platform})")
            # EXPECTED_HASH makes a failed download, a repointed tag or a tampered archive fatal here.
            file(DOWNLOAD
                "https://github.com/MafiaHub/Framework/releases/download/v${FRAMEWORK_PIN_VERSION}/${_fw_name}.${_fw_ext}"
                "${_fw_archive}"
                EXPECTED_HASH SHA256=${_fw_expected}
                SHOW_PROGRESS)
            # Unpacked beside its final name and moved into place whole, so an interrupted configure
            # never leaves a half tree that looks complete.
            set(_fw_partial "${_fw_cache}/${_fw_name}.partial")
            file(REMOVE_RECURSE "${_fw_partial}" "${_fw_sdk_root}")
            file(ARCHIVE_EXTRACT INPUT "${_fw_archive}" DESTINATION "${_fw_partial}")
            file(RENAME "${_fw_partial}/${_fw_name}" "${_fw_sdk_root}")
            file(REMOVE_RECURSE "${_fw_partial}")
            file(REMOVE "${_fw_archive}")
            file(WRITE "${_fw_stamp}" "${_fw_expected}
")
        endif()
    endif()

    if(NOT EXISTS "${_fw_sdk_root}/lib/cmake/Framework/FrameworkConfig.cmake")
        message(FATAL_ERROR "'${_fw_sdk_root}' is not a Framework SDK (no lib/cmake/Framework/FrameworkConfig.cmake)")
    endif()
    set(FW_BOOTSTRAP_MODE "sdk")
    set(FW_SDK_ROOT "${_fw_sdk_root}")
    set(Framework_DIR "${_fw_sdk_root}/lib/cmake/Framework" CACHE PATH "" FORCE)
    # vcpkg resolves the manifest the archive was built from, with its triplets.
    set(VCPKG_MANIFEST_DIR "${_fw_sdk_root}/share/framework" CACHE PATH "" FORCE)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "" FORCE)
    include("${_fw_sdk_root}/share/framework/VcpkgBootstrap.cmake")
endif()

message(STATUS "Framework: ${FW_BOOTSTRAP_MODE} mode")

macro(fw_bootstrap_finish)
    if(FW_BOOTSTRAP_MODE STREQUAL "source")
        list(APPEND CMAKE_MODULE_PATH "${FW_SOURCE_DIR}/cmake")
        include(FrameworkSetup)
        add_subdirectory("${FW_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/framework")
        set(Framework_SCRIPTS_DIR "${FW_SOURCE_DIR}/scripts")
    elseif(FW_BOOTSTRAP_MODE STREQUAL "sdk")
        list(APPEND CMAKE_MODULE_PATH "${Framework_DIR}")
        include(FrameworkSetup)
        find_package(Framework CONFIG REQUIRED)
    endif()
endmacro()
