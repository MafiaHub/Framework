# fw_stage_runtime(<target> CLIENT|SERVER)
#
# Puts the Framework runtime files a target needs next to it: the CEF distribution, libnode, the
# crash handler, the helper executables. Built against the SDK those come from the archive's
# runtime/<role>/ tree. Built from source (the Framework tree is a subdirectory) the Framework's own copy targets already fill ${CMAKE_BINARY_DIR}/bin, so only the
# dependencies on them and the Linux RPATH are set.
function(fw_stage_runtime target role)
    if(NOT role MATCHES "^(CLIENT|SERVER)$")
        message(FATAL_ERROR "fw_stage_runtime(${target}): role must be CLIENT or SERVER, got '${role}'")
    endif()
    if(UNIX AND NOT APPLE)
        # libnode.so ships next to the binary and resolves through $ORIGIN, so the bundle runs
        # without LD_LIBRARY_PATH.
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()

    if(NOT Framework_RUNTIME_DIR)
        if(TARGET libnode_copy_dll)
            add_dependencies(${target} libnode_copy_dll)
        endif()
        if(role STREQUAL "CLIENT" AND TARGET CEFCopyDlls)
            add_dependencies(${target} CEFCopyDlls)
        endif()
        if(role STREQUAL "SERVER" AND TARGET libnode AND UNIX AND NOT APPLE)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libnode>" "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Copying libnode next to ${target}" VERBATIM)
        endif()
        return()
    endif()

    string(TOLOWER "${role}" _subdir)
    if(NOT IS_DIRECTORY "${Framework_RUNTIME_DIR}/${_subdir}")
        message(FATAL_ERROR "fw_stage_runtime(${target}): this Framework SDK has no ${_subdir} runtime (${Framework_RUNTIME_DIR}/${_subdir})")
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.26)
        set(_copy copy_directory)
    else()
        set(_copy copy_directory_if_different)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E ${_copy} "${Framework_RUNTIME_DIR}/${_subdir}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Staging the Framework ${_subdir} runtime next to ${target}" VERBATIM)
endfunction()
