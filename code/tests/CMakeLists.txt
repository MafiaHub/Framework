# The GUI resource units under test are free of any CEF dependency on purpose,
# so they compile straight into the test binary rather than pulling in
# FrameworkClient and a browser.
add_executable(FrameworkTests
        framework_ut.cpp
        ../framework/src/voice/client/mixer.cpp
        ../framework/src/gui/resources/resource_path.cpp
        ../framework/src/gui/resources/mime.cpp
        ../framework/src/gui/resources/resource_stream.cpp
        ../framework/src/gui/resources/directory_provider.cpp
        ../framework/src/gui/resources/memory_provider.cpp
        # Free of CEF and of Integrations::Client::Instance, so the package mounter is testable
        # without linking FrameworkClient.
        ../framework/src/integrations/client/resource_packages.cpp)

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer libnode)


# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        DEPENDS $<TARGET_FILE:FrameworkTests>)
