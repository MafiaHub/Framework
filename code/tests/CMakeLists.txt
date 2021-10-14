add_executable(FrameworkTests framework_ut.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)

# Put test projects in the framework/tests folder in MSVC
if (MSVC)
    get_all_targets(all_targets)
    set_target_properties(${all_targets} PROPERTIES FOLDER framework/tests)
endif ()