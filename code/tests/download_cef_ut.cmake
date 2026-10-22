# Regression test for cmake/DownloadCEF.cmake, run in script mode by RunFrameworkTests:
#
#   cmake -DSCRATCH=<empty directory> -P download_cef_ut.cmake
#
# It packs a small stand-in distribution, so nothing is downloaded: the version
# does not exist on the CEF CDN, and any case that tried to fetch it fails.

cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DownloadCEF.cmake")

if(NOT SCRATCH)
    message(FATAL_ERROR "download_cef_ut: pass -DSCRATCH=<directory>")
endif()

set(platform "windows64")
set(version "0.0.0+gfwtest+chromium-0.0.0.0")
set(dist "cef_binary_${version}_${platform}_minimal")
set(cache "${SCRATCH}/cache")
set(root "${cache}/${dist}")
set(archive "${cache}/${dist}.tar.bz2")
set(files
    "include/cef_version.h"
    "libcef_dll/wrapper/libcef_dll_wrapper.cc"
    "Release/libcef.dll"
    "Release/libcef.lib"
    "Release/chrome_elf.dll"
    "Resources/icudtl.dat"
    "Resources/resources.pak"
    "Resources/locales/en-US.pak"
)

function(expect message)
    if(NOT (${ARGN}))
        message(FATAL_ERROR "download_cef_ut: ${message}")
    endif()
endfunction()

function(expect_complete what)
    foreach(file IN LISTS files)
        if(NOT EXISTS "${root}/${file}")
            message(FATAL_ERROR "download_cef_ut: ${what}: ${file} is missing")
        endif()
    endforeach()
    if(IS_DIRECTORY "${cache}/${dist}.extracting")
        message(FATAL_ERROR "download_cef_ut: ${what}: the temporary extraction was left behind")
    endif()
endfunction()

# The stand-in archive and its checksum, as a real download leaves them.
file(REMOVE_RECURSE "${SCRATCH}")
foreach(file IN LISTS files)
    file(WRITE "${SCRATCH}/source/${dist}/${file}" "${file}\n")
endforeach()
file(MAKE_DIRECTORY "${cache}")
execute_process(COMMAND ${CMAKE_COMMAND} -E tar cjf "${archive}" "${dist}" WORKING_DIRECTORY "${SCRATCH}/source" RESULT_VARIABLE packed)
expect("could not pack the stand-in archive" packed EQUAL 0)
file(SHA1 "${archive}" archive_sha1)
file(WRITE "${archive}.sha1" "${archive_sha1}\n")

# A fresh directory extracts from the cached archive and stamps the result.
DownloadCEF("${platform}" "${version}" "${cache}")
expect("CEF_ROOT is ${CEF_ROOT}, expected ${root}" CEF_ROOT STREQUAL root)
expect_complete("fresh extraction")
expect("a fresh extraction was not stamped" EXISTS "${root}/.fw-extracted")

# A file the build needs that went missing after extraction is restored, stamp or not.
file(REMOVE "${root}/Release/libcef.lib")
DownloadCEF("${platform}" "${version}" "${cache}")
expect_complete("stamped extraction missing libcef.lib")
expect("a restored extraction was not stamped again" EXISTS "${root}/.fw-extracted")

# A stamped extraction is used as it is, even with the archive gone.
file(RENAME "${archive}" "${archive}.kept")
DownloadCEF("${platform}" "${version}" "${cache}")
file(RENAME "${archive}.kept" "${archive}")

# An extraction from before the stamp is kept in place when it is complete.
file(REMOVE "${root}/.fw-extracted")
file(WRITE "${root}/kept.txt" "kept\n")
DownloadCEF("${platform}" "${version}" "${cache}")
expect("a complete unstamped extraction was replaced" EXISTS "${root}/kept.txt")
expect("a complete unstamped extraction was not stamped" EXISTS "${root}/.fw-extracted")

# One missing a file is extracted again.
file(REMOVE "${root}/.fw-extracted" "${root}/Release/libcef.lib")
DownloadCEF("${platform}" "${version}" "${cache}")
expect_complete("repaired extraction")
expect("a repaired extraction was not stamped" EXISTS "${root}/.fw-extracted")
expect("an incomplete extraction was kept" NOT EXISTS "${root}/kept.txt")

# An extraction an interrupted configure left half done is cleared and redone.
file(REMOVE_RECURSE "${root}")
file(WRITE "${cache}/${dist}.extracting/${dist}/include/cef_version.h" "partial\n")
DownloadCEF("${platform}" "${version}" "${cache}")
expect_complete("extraction after an interrupted one")

# Without the archive, an unstamped extraction holding what the build needs is
# used without a download, and stays unstamped.
file(REMOVE "${root}/.fw-extracted")
file(RENAME "${archive}" "${archive}.kept")
DownloadCEF("${platform}" "${version}" "${cache}")
expect("an extraction checked without its archive was stamped" NOT EXISTS "${root}/.fw-extracted")
file(RENAME "${archive}.kept" "${archive}")

message(STATUS "download_cef_ut: passed")
