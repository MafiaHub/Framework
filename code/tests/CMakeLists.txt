add_executable(FrameworkTests framework_ut.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer libnode)

# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

# Reuse FrameworkServer's PCH (includes Framework's headers and flecs)
target_precompile_headers(FrameworkTests REUSE_FROM FrameworkServer)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)
