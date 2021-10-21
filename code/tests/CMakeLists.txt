add_executable(FrameworkTests framework_ut.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)
