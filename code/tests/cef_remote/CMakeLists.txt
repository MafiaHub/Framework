# Remote CEF: the wire protocol, the shared-memory frame surface, the pipe and the fw:// flow control
# between the host's broker and the client's server on their own (FrameworkCefRemoteTests), then the whole path against the real 64-bit host - start it, load a
# page from a client-served fw:// root, read a frame back, kill the host and watch it come back -
# from a client of this tree's architecture (FrameworkCefHostSmoke).

add_executable(FrameworkCefRemoteTests
    cef_remote_ut.cpp
    ../../framework/src/gui/remote/channel.cpp
    ../../framework/src/gui/remote/frame_buffer.cpp
    ../../framework/src/gui/remote/protocol.cpp
    ../../framework/src/gui/remote/resource_server.cpp
    ../../framework/src/gui/remote/host/remote_provider.cpp
    ../../framework/src/gui/resources/memory_provider.cpp
    ../../framework/src/gui/resources/resource_stream.cpp)
target_include_directories(FrameworkCefRemoteTests PRIVATE .. ../../framework/src)
# Framework for the logger the host's broker writes to; nothing here touches CEF.
target_link_libraries(FrameworkCefRemoteTests PRIVATE Framework function2::function2)
target_compile_definitions(FrameworkCefRemoteTests PRIVATE NOMINMAX)

add_executable(FrameworkCefHostSmoke cef_host_smoke.cpp)
target_include_directories(FrameworkCefHostSmoke PRIVATE ../../framework/src)
target_link_libraries(FrameworkCefHostSmoke PRIVATE FrameworkClient Framework)
target_compile_definitions(FrameworkCefHostSmoke PRIVATE NOMINMAX _USE_MATH_DEFINES)
if(TARGET FrameworkCefHost)
    add_dependencies(FrameworkCefHostSmoke FrameworkCefHost)
endif()

# Both next to bin/cef64, where a client finds the host.
set_target_properties(FrameworkCefRemoteTests FrameworkCefHostSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_custom_target(RunFrameworkCefRemoteTests
    COMMAND $<TARGET_FILE:FrameworkCefRemoteTests>
    COMMAND $<TARGET_FILE:FrameworkCefHostSmoke>
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    DEPENDS FrameworkCefRemoteTests FrameworkCefHostSmoke
    USES_TERMINAL)
