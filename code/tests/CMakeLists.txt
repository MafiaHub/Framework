add_executable(FrameworkTests framework_ut.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer libnode)

if(FW_METRICS_BACKEND STREQUAL "prometheus")
    target_compile_definitions(FrameworkTests PRIVATE FW_METRICS_BACKEND_PROMETHEUS=1)
endif()

# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)
