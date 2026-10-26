# Load the framework
add_subdirectory(framework)

option(FW_BUILD_TESTS "Build the Framework test binaries" ${PROJECT_IS_TOP_LEVEL})
if(FW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

