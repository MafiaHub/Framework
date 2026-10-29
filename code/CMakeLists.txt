# Load the framework
add_subdirectory(framework)

option(FW_BUILD_TESTS "Build the Framework test binaries" ${PROJECT_IS_TOP_LEVEL})
if(FW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The remote CEF tests need only the client half, so a project's client tree can have them without
# the rest of the suite: configure it with -DFW_BUILD_CEF_REMOTE_TESTS=ON (Mafia1Online: set
# M1O_CMAKE_ARGS=-DFW_BUILD_CEF_REMOTE_TESTS=ON for one builds/build.bat run; the cache keeps it),
# then build RunFrameworkCefRemoteTests.
option(FW_BUILD_CEF_REMOTE_TESTS "Build the remote CEF tests (RunFrameworkCefRemoteTests)" ${FW_BUILD_TESTS})
if(FW_BUILD_CEF_REMOTE_TESTS AND WIN32 AND TARGET FrameworkClient)
    add_subdirectory(tests/cef_remote EXCLUDE_FROM_ALL)
endif()
