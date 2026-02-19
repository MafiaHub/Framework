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
#   download_dir - Directory to download and extract into
#
# Sets the following cache variable:
#   CEF_ROOT - Path to the extracted CEF distribution directory

function(DownloadCEF platform version download_dir)
    # Use minimal distribution (no sample apps, significantly smaller download)
    set(CEF_DISTRIBUTION "cef_binary_${version}_${platform}_minimal")
    set(CEF_DOWNLOAD_DIR "${download_dir}")

    # The location where we expect the extracted binary distribution
    set(CEF_ROOT "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}" CACHE INTERNAL "CEF_ROOT")

    # Skip if already extracted
    if(IS_DIRECTORY "${CEF_ROOT}")
        message(STATUS "CEF: Using existing distribution at ${CEF_ROOT}")
        return()
    endif()

    set(CEF_DOWNLOAD_FILENAME "${CEF_DISTRIBUTION}.tar.bz2")
    set(CEF_DOWNLOAD_PATH "${CEF_DOWNLOAD_DIR}/${CEF_DOWNLOAD_FILENAME}")

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

        # Download the binary distribution with hash verification
        message(STATUS "CEF: Downloading ${CEF_DOWNLOAD_FILENAME} (~100MB, this may take a while) ...")
        file(DOWNLOAD "${CEF_DOWNLOAD_URL_ESCAPED}" "${CEF_DOWNLOAD_PATH}"
            EXPECTED_HASH SHA1=${CEF_SHA1}
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            file(REMOVE "${CEF_DOWNLOAD_PATH}")
            message(FATAL_ERROR "CEF: Download failed: ${ERROR_MSG}\n  URL: ${CEF_DOWNLOAD_URL_ESCAPED}")
        endif()

        message(STATUS "CEF: Download complete.")
    else()
        message(STATUS "CEF: Using cached archive ${CEF_DOWNLOAD_PATH}")
    endif()

    # Extract the binary distribution
    message(STATUS "CEF: Extracting ${CEF_DOWNLOAD_FILENAME} ...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${CEF_DOWNLOAD_PATH}"
        WORKING_DIRECTORY "${CEF_DOWNLOAD_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "CEF: Extraction failed (exit code ${EXTRACT_RESULT})")
    endif()

    # Verify extraction succeeded
    if(NOT IS_DIRECTORY "${CEF_ROOT}")
        message(FATAL_ERROR "CEF: Extraction completed but expected directory not found: ${CEF_ROOT}")
    endif()

    message(STATUS "CEF: Ready at ${CEF_ROOT}")
endfunction()
