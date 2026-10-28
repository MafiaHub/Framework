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
        # Plain HTTP and hashing, so the asset download is testable against the server's file route.
        ../framework/src/integrations/client/asset_downloader.cpp
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
    target_link_libraries(FrameworkTests d3d12 dxgi d3dcompiler imgui::imgui)

    add_executable(FrameworkSentryTests sentry_ut.cpp)
    target_include_directories(FrameworkSentryTests PRIVATE ../framework/src)
    target_link_libraries(FrameworkSentryTests PRIVATE Framework)
    add_custom_target(RunFrameworkSentryTests
        COMMAND $<TARGET_FILE:FrameworkSentryTests>
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        DEPENDS FrameworkSentryTests)
endif()

target_include_directories(FrameworkTests PRIVATE . ../framework/src)
target_link_libraries(FrameworkTests Framework FrameworkServer libnode)


# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

add_custom_target(RunFrameworkTests
        COMMAND $<TARGET_FILE:FrameworkTests>
        COMMAND $<TARGET_FILE:FrameworkClientScriptingTests>
        COMMAND ${CMAKE_COMMAND} "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/download-cef-ut" -P "${CMAKE_CURRENT_SOURCE_DIR}/download_cef_ut.cmake"
        DEPENDS FrameworkTests FrameworkClientScriptingTests)
if(WIN32)
    add_dependencies(RunFrameworkTests RunFrameworkSentryTests)
endif()

# NodeEngine and V8Engine each initialize V8's process-wide platform, so exercise
# the client runtime in its own process.
add_executable(FrameworkClientScriptingTests client_scripting_ut.cpp)
target_include_directories(FrameworkClientScriptingTests PRIVATE . ../framework/src)
if(WIN32)
    # Key.bind dispatch, without linking the rest of FrameworkClient.
    target_sources(FrameworkClientScriptingTests PRIVATE
        ../framework/src/integrations/client/scripting/builtins/keybinds.cpp
        ../framework/src/utils/key_names.cpp)
endif()
target_link_libraries(FrameworkClientScriptingTests Framework FrameworkServer libnode)

# The SDK smoke test: install this build into a prefix, then configure and build a consumer project
# against that prefix alone (code/tests/sdk-consumer), the way a mod does through
# cmake/FrameworkBootstrap.cmake. The vcpkg checkout is shared so the consumer's manifest install
# comes out of the same binary cache.
set(_fw_sdk_prefix "${CMAKE_BINARY_DIR}/sdk")
set(_fw_sdk_consumer "${CMAKE_BINARY_DIR}/sdk-consumer")
set(_fw_sdk_consumer_args
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=$<CONFIG>"
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DFW_SDK_ROOT=${_fw_sdk_prefix}"
    "-DFW_VCPKG_ROOT=${FW_VCPKG_ROOT_RESOLVED}")
if(CMAKE_PROJECT_INCLUDE_BEFORE)
    list(APPEND _fw_sdk_consumer_args "-DCMAKE_PROJECT_INCLUDE_BEFORE=${CMAKE_PROJECT_INCLUDE_BEFORE}")
endif()
add_custom_target(FrameworkSdkSmoke
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_fw_sdk_prefix}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${_fw_sdk_prefix}" --config $<CONFIG>
    COMMAND ${CMAKE_COMMAND} -S "${CMAKE_CURRENT_SOURCE_DIR}/sdk-consumer" -B "${_fw_sdk_consumer}" ${_fw_sdk_consumer_args}
    COMMAND ${CMAKE_COMMAND} --build "${_fw_sdk_consumer}" --config $<CONFIG>
    COMMENT "Installing the SDK to ${_fw_sdk_prefix} and building code/tests/sdk-consumer against it"
    VERBATIM USES_TERMINAL)
# Every exported target must exist before the install runs, including vendors nothing on this
# platform links (libsig without a server, for instance).
get_property(_fw_sdk_smoke_deps GLOBAL PROPERTY FW_SDK_EXPORTED_TARGETS)
list(APPEND _fw_sdk_smoke_deps TracyClient)
if(TARGET libnode_node_api_def)
    list(APPEND _fw_sdk_smoke_deps libnode_node_api_def)
endif()
add_dependencies(FrameworkSdkSmoke ${_fw_sdk_smoke_deps})

# Native input primitives can be tested without building or launching clients.
add_executable(FrameworkInputTests input_ut.cpp)
target_include_directories(FrameworkInputTests PRIVATE . ../framework/src)
if(WIN32)
    target_link_libraries(FrameworkInputTests PRIVATE user32)
endif()
add_custom_target(RunFrameworkInputTests
    COMMAND $<TARGET_FILE:FrameworkInputTests>
    DEPENDS FrameworkInputTests)
