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

# The pattern table, the base-relocation helpers and the Epic manifest scan live in
# FrameworkClient/FrameworkLoader, neither of which the tests link. Compile them and the
# standalone D3D12 backend and compositor directly; their regressions use WARP without a game or browser.
if(WIN32)
    target_sources(FrameworkTests PRIVATE
        ../framework/src/utils/hooking/pattern_table.cpp
        ../framework/src/utils/hooking/hooking_patterns.cpp
        ../framework/src/utils/hooking/hooking.cpp
        ../framework/src/graphics/backend/d3d12.cpp
        ../framework/src/graphics/backend/d3d12_ui_compositor.cpp
        ../framework/src/external/epic/manifest.cpp)
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

# Not a test: what a server tick's replication costs as players gather (see the file). Build it in
# RelWithDebInfo or Release.
add_executable(FrameworkReplicationBench bench/replication_bench.cpp)
target_include_directories(FrameworkReplicationBench PRIVATE ../framework/src)
target_link_libraries(FrameworkReplicationBench Framework FrameworkServer libnode)

# Not a test: what copying a value between two resources costs (see the file). Build it in
# RelWithDebInfo or Release.
add_executable(FrameworkValueTransferBench bench/value_transfer_bench.cpp)
target_include_directories(FrameworkValueTransferBench PRIVATE ../framework/src)
target_link_libraries(FrameworkValueTransferBench Framework FrameworkServer libnode)
target_compile_definitions(FrameworkValueTransferBench PRIVATE NODE_WANT_INTERNALS=1)

# Not a test: what a log call costs its caller while the outputs keep up, fall behind and stall (see the
# file). Build it in RelWithDebInfo or Release.
add_executable(FrameworkLoggingBench bench/logging_bench.cpp)
target_include_directories(FrameworkLoggingBench PRIVATE ../framework/src)
target_link_libraries(FrameworkLoggingBench Framework)


# Tests use Node.js APIs through the scripting engine
target_compile_definitions(FrameworkTests PRIVATE NODE_WANT_INTERNALS=1)

# Committed files a test compares against, read from the source tree so that a test asked to
# regenerate one rewrites the tracked copy.
target_compile_definitions(FrameworkTests PRIVATE FW_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

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
# The Key and Voice builtins of a client and what they stand on, without linking the rest of
# FrameworkClient: key names, and a voice client that is never attached to a peer and so opens no device.
target_sources(FrameworkClientScriptingTests PRIVATE
    ../framework/src/integrations/client/scripting/builtins/keybinds.cpp
    ../framework/src/integrations/client/scripting/builtins/voice.cpp
    ../framework/src/utils/key_names.cpp
    ../framework/src/voice/client/mixer.cpp
    ../framework/src/voice/client/audio_device.cpp
    ../framework/src/voice/client/noise_suppressor.cpp
    ../framework/src/voice/client/voice_client.cpp
    ../framework/src/networking/network_client.cpp)
# The vendors build miniaudio for the client, which is Windows; the voice client needs it wherever it links.
if(NOT TARGET miniaudio)
    add_subdirectory(../../vendors/miniaudio "${CMAKE_BINARY_DIR}/vendors/miniaudio" EXCLUDE_FROM_ALL)
endif()
target_link_libraries(FrameworkClientScriptingTests Framework FrameworkServer libnode miniaudio)
target_compile_definitions(FrameworkClientScriptingTests PRIVATE FW_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The Chat and Discord builtins of a client reach their services through the client instance, which builds
# on Windows only and with the whole of FrameworkClient. client_doubles has a stand-in for it, ahead of the
# framework on the include path, and the functions of the chat box and of the Discord wrapper that the
# builtins call. The activity type of Discord is plain data and is built from the vendored source.
# The Web builtin reaches the web views through the GUI manager, which needs CEF and Direct3D: client_doubles
# has a stand-in for the manager and for a view too (gui/manager.h, gui/view.h), which keep what they are told.
target_include_directories(FrameworkClientScriptingTests BEFORE PRIVATE client_doubles)
target_include_directories(FrameworkClientScriptingTests PRIVATE ../../vendors/discord/src)
target_sources(FrameworkClientScriptingTests PRIVATE
    client_doubles/client_doubles.cpp
    ../framework/src/integrations/client/scripting/builtins/chat.cpp
    ../framework/src/integrations/client/scripting/builtins/discord.cpp
    ../framework/src/integrations/client/scripting/builtins/web.cpp
    ../../vendors/discord/src/types.cpp)
# Each stand-in is another class under the name of the real one. That is sound only while the real ones are
# not in this binary: checked once everything in this directory has had its say.
function(_fw_refuse_client_in_scripting_tests)
    get_target_property(_linked FrameworkClientScriptingTests LINK_LIBRARIES)
    foreach(_library FrameworkClient Framework::Client)
        if(_library IN_LIST _linked)
            message(FATAL_ERROR "FrameworkClientScriptingTests links ${_library}, and builds its client builtins against "
                "the stand-ins of code/tests/client_doubles for Integrations::Client::Instance, GUI::Manager and GUI::View: "
                "two definitions of one class. Remove client_doubles from the target, and test against the real classes, "
                "before linking it.")
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL _fw_refuse_client_in_scripting_tests)
# Nametags reads the switches of the ImGui nametag widget, so it is tested where the headers of ImGui are:
# in the vcpkg tree of this build, which has them on Windows, or where FW_TEST_IMGUI_HEADERS is set to a
# copy of them. Never a copy found on the host: the variable was FW_TEST_IMGUI_INCLUDE_DIR while a search of the
# host could fill it, and a build directory may still have that entry, which is dropped.
unset(FW_TEST_IMGUI_INCLUDE_DIR CACHE)
set(FW_TEST_IMGUI_HEADERS "" CACHE PATH "Directory of the ImGui headers the client Nametags builtin is tested with; empty for those of vcpkg")
set(_fw_test_imgui "")
if(FW_TEST_IMGUI_HEADERS)
    set(_fw_test_imgui "${FW_TEST_IMGUI_HEADERS}")
    if(NOT EXISTS "${_fw_test_imgui}/imgui.h")
        message(FATAL_ERROR "FW_TEST_IMGUI_HEADERS is '${_fw_test_imgui}', which has no imgui.h")
    endif()
elseif(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/imgui.h")
    set(_fw_test_imgui "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
endif()
if(_fw_test_imgui)
    target_sources(FrameworkClientScriptingTests PRIVATE ../framework/src/integrations/client/scripting/builtins/nametags.cpp)
    target_include_directories(FrameworkClientScriptingTests PRIVATE "${_fw_test_imgui}")
    target_compile_definitions(FrameworkClientScriptingTests PRIVATE FW_TEST_CLIENT_NAMETAGS=1)
endif()

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
