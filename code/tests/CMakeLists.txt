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
        ../framework/src/integrations/client/resource_packages.cpp
        # The transport half of the client, for the admission handshake over loopback. It needs
        # nothing of FrameworkClient beyond itself.
        ../framework/src/networking/network_client.cpp)

# The pattern table and the base-relocation helpers live in FrameworkClient/FrameworkLoader,
# neither of which the tests link. Compile them and the standalone D3D12 compositor
# directly; the compositor regression uses WARP without a game or browser.
if(WIN32)
    target_sources(FrameworkTests PRIVATE
        ../framework/src/utils/hooking/pattern_table.cpp
        ../framework/src/utils/hooking/hooking_patterns.cpp
        ../framework/src/utils/hooking/hooking.cpp
        ../framework/src/graphics/backend/d3d12_ui_compositor.cpp)
    target_link_libraries(FrameworkTests d3d12 dxgi d3dcompiler)
endif()

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer libnode)


# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        COMMAND $<TARGET_FILE:FrameworkClientScriptingTests>
        DEPENDS FrameworkTests FrameworkClientScriptingTests)

# NodeEngine and V8Engine each initialize V8's process-wide platform, so exercise
# the client runtime in its own process.
add_executable(FrameworkClientScriptingTests client_scripting_ut.cpp)
target_include_directories(FrameworkClientScriptingTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkClientScriptingTests Framework FrameworkServer libnode)

# Native input primitives can be tested without building or launching clients.
add_executable(FrameworkInputTests input_ut.cpp)
target_include_directories(FrameworkInputTests PRIVATE . ../framework/src)
if(WIN32)
    target_link_libraries(FrameworkInputTests PRIVATE user32)
endif()
add_custom_target(RunFrameworkInputTests
    COMMAND $<TARGET_FILE:FrameworkInputTests>
    DEPENDS FrameworkInputTests)
