# DownloadCEF.cmake
#
# Downloads and extracts a CEF binary distribution at CMake configure time.
# Based on the official chromiumembedded/cef-project approach.
#
# Usage:
#   DownloadCEF(<platform> <version> <download_dir>)
#
# Where:
#   platform     - "windows32" or "windows64"
#   version      - CEF version string (e.g. "144.0.6+g5f7e671+chromium-144.0.7559.59")
#   download_dir - Directory to download and extract into. Several checkouts may
#                  share one: a lock keeps them from working on it at once.
#
# Sets the following cache variable:
#   CEF_ROOT - Path to the extracted CEF distribution directory
#
# An extraction is trusted only once it has finished. It is unpacked beside its
# final name and moved into place whole, and a stamp is written last. A
# distribution directory without the stamp - one an interrupted configure left
# behind, or one from before the stamp existed - is checked against the
# archive: kept and stamped when complete, extracted again when anything is
# missing. The files the build cannot do without are checked on every
# configure, so one removed after extraction is restored rather than failing
# the link.

# Sets <out_var> to the first file the build needs that <root> lacks, or to "".
function(_fw_cef_missing_required root out_var)
    foreach(required IN ITEMS "include/cef_version.h" "libcef_dll" "Release/libcef.dll" "Release/libcef.lib" "Resources/icudtl.dat")
        if(NOT EXISTS "${root}/${required}")
            set(${out_var} "${required}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(DownloadCEF platform version download_dir)
    # Use minimal distribution (no sample apps, significantly smaller download)
    set(CEF_DISTRIBUTION "cef_binary_${version}_${platform}_minimal")
    set(CEF_DOWNLOAD_DIR "${download_dir}")

    # The location where we expect the extracted binary distribution
    set(CEF_ROOT "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}" CACHE INTERNAL "CEF_ROOT")
    set(CEF_STAMP "${CEF_ROOT}/.fw-extracted")

    # Skip if a finished extraction is already there
    _fw_cef_missing_required("${CEF_ROOT}" CEF_MISSING_REQUIRED)
    if(EXISTS "${CEF_STAMP}" AND CEF_MISSING_REQUIRED STREQUAL "")
        message(STATUS "CEF: Using existing distribution at ${CEF_ROOT}")
        return()
    endif()

    # Another checkout sharing this directory may be downloading or extracting
    # the same distribution; wait for it, then look again.
    file(MAKE_DIRECTORY "${CEF_DOWNLOAD_DIR}")
    file(LOCK "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}.lock" GUARD FUNCTION RESULT_VARIABLE LOCK_RESULT TIMEOUT 3600)
    if(NOT LOCK_RESULT EQUAL 0)
        message(FATAL_ERROR "CEF: Could not lock ${CEF_DOWNLOAD_DIR} for ${CEF_DISTRIBUTION}: ${LOCK_RESULT}")
    endif()
    _fw_cef_missing_required("${CEF_ROOT}" CEF_MISSING_REQUIRED)
    if(EXISTS "${CEF_STAMP}")
        if(CEF_MISSING_REQUIRED STREQUAL "")
            message(STATUS "CEF: Using existing distribution at ${CEF_ROOT}")
            return()
        endif()
        file(REMOVE "${CEF_STAMP}")
    endif()

    set(CEF_DOWNLOAD_FILENAME "${CEF_DISTRIBUTION}.tar.bz2")
    set(CEF_DOWNLOAD_PATH "${CEF_DOWNLOAD_DIR}/${CEF_DOWNLOAD_FILENAME}")

    # A cached archive is only as good as its checksum: one cut short by an
    # interrupted download is dropped and fetched again.
    if(EXISTS "${CEF_DOWNLOAD_PATH}" AND EXISTS "${CEF_DOWNLOAD_PATH}.sha1")
        file(READ "${CEF_DOWNLOAD_PATH}.sha1" CEF_SHA1)
        string(STRIP "${CEF_SHA1}" CEF_SHA1)
        file(SHA1 "${CEF_DOWNLOAD_PATH}" CEF_ARCHIVE_SHA1)
        if(NOT CEF_ARCHIVE_SHA1 STREQUAL CEF_SHA1)
            message(WARNING "CEF: Cached archive ${CEF_DOWNLOAD_PATH} does not match its checksum; downloading it again")
            file(REMOVE "${CEF_DOWNLOAD_PATH}" "${CEF_DOWNLOAD_PATH}.sha1")
        endif()
    endif()

    # An unstamped distribution directory: keep it if nothing is missing.
    if(IS_DIRECTORY "${CEF_ROOT}")
        if(EXISTS "${CEF_DOWNLOAD_PATH}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar tf "${CEF_DOWNLOAD_PATH}"
                OUTPUT_VARIABLE CEF_ARCHIVE_LISTING
                RESULT_VARIABLE LIST_RESULT
            )
            set(CEF_MISSING "")
            if(LIST_RESULT EQUAL 0)
                string(REGEX MATCHALL "[^\r\n]+" CEF_ARCHIVE_ENTRIES "${CEF_ARCHIVE_LISTING}")
                foreach(entry IN LISTS CEF_ARCHIVE_ENTRIES)
                    if(NOT EXISTS "${CEF_DOWNLOAD_DIR}/${entry}")
                        set(CEF_MISSING "${entry}")
                        break()
                    endif()
                endforeach()
            else()
                set(CEF_MISSING "(the archive could not be listed)")
            endif()
            if(CEF_MISSING STREQUAL "")
                file(WRITE "${CEF_STAMP}" "${CEF_DISTRIBUTION}\n")
                message(STATUS "CEF: Using existing distribution at ${CEF_ROOT}")
                return()
            endif()
            message(WARNING "CEF: ${CEF_ROOT} is incomplete (missing ${CEF_MISSING}); extracting it again")
        else()
            # Without the archive there is no listing to check against, so check
            # what the build itself needs, and leave it unstamped.
            if(CEF_MISSING_REQUIRED STREQUAL "")
                message(STATUS "CEF: Using existing distribution at ${CEF_ROOT}")
                return()
            endif()
            message(WARNING "CEF: ${CEF_ROOT} is incomplete (missing ${CEF_MISSING_REQUIRED}); downloading it again")
        endif()
    endif()

    # Build URL with '+' encoded as '%2B'
    set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_DOWNLOAD_FILENAME}")
    string(REPLACE "+" "%2B" CEF_DOWNLOAD_URL_ESCAPED "${CEF_DOWNLOAD_URL}")

    if(NOT EXISTS "${CEF_DOWNLOAD_PATH}")
        # Download SHA1 hash for verification
        message(STATUS "CEF: Downloading checksum ...")
        file(DOWNLOAD "${CEF_DOWNLOAD_URL_ESCAPED}.sha1" "${CEF_DOWNLOAD_PATH}.sha1"
            STATUS DOWNLOAD_SHA1_STATUS
        )
        list(GET DOWNLOAD_SHA1_STATUS 0 SHA1_STATUS_CODE)
        if(NOT SHA1_STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_SHA1_STATUS 1 SHA1_ERROR_MSG)
            file(REMOVE "${CEF_DOWNLOAD_PATH}.sha1")
            message(FATAL_ERROR
                "CEF: Failed to download SHA1 checksum: ${SHA1_ERROR_MSG}\n"
                "  URL: ${CEF_DOWNLOAD_URL_ESCAPED}.sha1\n"
                "  Check that CEF_VERSION is valid at https://cef-builds.spotifycdn.com/index.html"
            )
        endif()

        file(READ "${CEF_DOWNLOAD_PATH}.sha1" CEF_SHA1)
        string(STRIP "${CEF_SHA1}" CEF_SHA1)

        # Validate SHA1 looks like a 40-char hex string
        string(LENGTH "${CEF_SHA1}" SHA1_LEN)
        if(NOT SHA1_LEN EQUAL 40)
            file(REMOVE "${CEF_DOWNLOAD_PATH}.sha1")
            message(FATAL_ERROR
                "CEF: Downloaded SHA1 checksum is invalid (length ${SHA1_LEN}, expected 40).\n"
                "  URL: ${CEF_DOWNLOAD_URL_ESCAPED}.sha1\n"
                "  Check that CEF_VERSION is valid at https://cef-builds.spotifycdn.com/index.html"
            )
        endif()

        # Download the binary distribution with hash verification, under a
        # temporary name so an interrupted download is never mistaken for the archive
        message(STATUS "CEF: Downloading ${CEF_DOWNLOAD_FILENAME} (~100MB, this may take a while) ...")
        file(DOWNLOAD "${CEF_DOWNLOAD_URL_ESCAPED}" "${CEF_DOWNLOAD_PATH}.part"
            EXPECTED_HASH SHA1=${CEF_SHA1}
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            file(REMOVE "${CEF_DOWNLOAD_PATH}.part")
            message(FATAL_ERROR "CEF: Download failed: ${ERROR_MSG}\n  URL: ${CEF_DOWNLOAD_URL_ESCAPED}")
        endif()
        file(RENAME "${CEF_DOWNLOAD_PATH}.part" "${CEF_DOWNLOAD_PATH}")

        message(STATUS "CEF: Download complete.")
    else()
        message(STATUS "CEF: Using cached archive ${CEF_DOWNLOAD_PATH}")
    endif()

    # Extract beside the final directory and move it into place whole, so a
    # configure cut short leaves nothing under the name the build trusts
    set(CEF_EXTRACT_DIR "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}.extracting")
    file(REMOVE_RECURSE "${CEF_EXTRACT_DIR}")
    file(MAKE_DIRECTORY "${CEF_EXTRACT_DIR}")
    message(STATUS "CEF: Extracting ${CEF_DOWNLOAD_FILENAME} ...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${CEF_DOWNLOAD_PATH}"
        WORKING_DIRECTORY "${CEF_EXTRACT_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        file(REMOVE_RECURSE "${CEF_EXTRACT_DIR}")
        message(FATAL_ERROR "CEF: Extraction failed (exit code ${EXTRACT_RESULT})")
    endif()

    # Verify extraction succeeded
    if(NOT IS_DIRECTORY "${CEF_EXTRACT_DIR}/${CEF_DISTRIBUTION}")
        file(REMOVE_RECURSE "${CEF_EXTRACT_DIR}")
        message(FATAL_ERROR "CEF: Extraction completed but expected directory not found: ${CEF_EXTRACT_DIR}/${CEF_DISTRIBUTION}")
    endif()

    file(REMOVE_RECURSE "${CEF_ROOT}")
    file(RENAME "${CEF_EXTRACT_DIR}/${CEF_DISTRIBUTION}" "${CEF_ROOT}")
    file(REMOVE_RECURSE "${CEF_EXTRACT_DIR}")
    file(WRITE "${CEF_STAMP}" "${CEF_DISTRIBUTION}\n")

    message(STATUS "CEF: Ready at ${CEF_ROOT}")
endfunction()
