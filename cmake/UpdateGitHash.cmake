include(GetGitRevisionDescription)

macro(update_git_version mod_prefix in_file out_file)
    GET_GIT_HEAD_REVISION(GIT_REFSPEC ${mod_prefix}_GIT_VERSION_LONG)
    file(STRINGS ${PROJECT_SOURCE_DIR}/VERSION ${mod_prefix}_VERSION)

    if(NOT GIT_FOUND)
        find_package(Git QUIET)
    endif()

    set(TMP_GIT_VERSION ${${mod_prefix}_GIT_VERSION_LONG})
    message(STATUS "Project: ${mod_prefix} SHA1: ${TMP_GIT_VERSION} build: ${${mod_prefix}_VERSION}")

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short ${TMP_GIT_VERSION}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE res
        OUTPUT_VARIABLE ${mod_prefix}_GIT_VERSION
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

    configure_file(${in_file} ${out_file} @ONLY)
endmacro()
