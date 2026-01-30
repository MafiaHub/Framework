add_executable(FrameworkTests framework_ut.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_include_directories(FrameworkTests SYSTEM PRIVATE ${LIBNODE_UV_INCLUDE_DIR})
target_link_libraries(FrameworkTests Framework FrameworkServer libnode)

# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

# Reuse Framework's PCH (includes standard headers and flecs)
# Note: We reuse from Framework directly because FrameworkServer itself uses REUSE_FROM Framework
target_precompile_headers(FrameworkTests REUSE_FROM Framework)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)
