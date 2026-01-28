add_executable(FrameworkTests framework_ut.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer)

# Reuse FrameworkServer's PCH (includes sol, flecs, and Framework's headers)
target_precompile_headers(FrameworkTests REUSE_FROM FrameworkServer)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)
