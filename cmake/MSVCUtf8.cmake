if(WIN32 AND CMAKE_GENERATOR MATCHES "^Ninja")
    # MSVC localizes /showIncludes and encodes it using the console code page.
    # Keep CMake's prefix detection in the same UTF-8 encoding used for builds.
    execute_process(
        COMMAND "$ENV{COMSPEC}" /d /c "chcp 65001 >nul"
        RESULT_VARIABLE _fwUtf8Result
    )

    if(NOT _fwUtf8Result EQUAL 0)
        message(WARNING "Unable to select the UTF-8 console code page for MSVC dependency detection")
    endif()
endif()
