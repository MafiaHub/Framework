include(UpdateGitHash)
update_git_version(FW "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/version.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")

# List the source files
set(FRAMEWORK_SRC
    src/logging/logger.cpp

    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    src/utils/delay_scope.cpp
    src/utils/config.cpp
    src/utils/interpolator.cpp
    src/utils/time.cpp
    src/utils/command_listener.cpp
    src/utils/command_processor.cpp
    src/utils/hashing.cpp
    src/utils/path.cpp
    src/utils/safe_string.cpp
    src/utils/states/machine.cpp

    src/external/sentry/wrapper.cpp

    src/networking/network_peer.cpp
    src/networking/errors.cpp

    src/networking/replication/network_entity.cpp
    src/networking/replication/entity_factory.cpp
    src/networking/replication/interest_grid.cpp
    src/networking/replication/replication_manager.cpp
    src/networking/replication/replication_connection.cpp

    # JavaScript scripting engine base and builtins (used by both client and server)
    # Note: node_engine.cpp and v8_engine.cpp are in OBJECT libraries due to special compile flags
    src/scripting/engine.cpp
    src/scripting/builtins/vector2.cpp
    src/scripting/builtins/vector3.cpp
    src/scripting/builtins/vector4.cpp
    src/scripting/builtins/quaternion.cpp
    src/scripting/builtins/color.cpp
    src/scripting/builtins/events.cpp
    src/scripting/builtins/messages.cpp
    src/scripting/builtins/console.cpp
    src/scripting/builtins/imports.cpp
    src/scripting/builtins/exports.cpp
    src/scripting/builtins/environment.cpp
    src/scripting/resource/package_manifest.cpp
    src/scripting/resource/resource.cpp
    src/scripting/resource/resource_manager.cpp

    src/jobs/job_system.cpp
    src/jobs/io_tasks.cpp
)

set(FRAMEWORK_SERVER_SRC
    src/http/webserver.cpp

    src/networking/network_server.cpp

    src/integrations/server/instance.cpp
    src/integrations/server/networking/engine.cpp

    # JavaScript scripting (server module)
    src/integrations/server/scripting/module.cpp
)

set(FRAMEWORK_CLIENT_SRC
    src/graphics/renderer.cpp
    src/graphics/renderio.cpp
    src/graphics/types.cpp

    src/networking/network_client.cpp

    src/external/steam/wrapper.cpp
    src/external/discord/wrapper.cpp
    src/external/imgui/wrapper.cpp

    src/external/imgui/widgets/ui_base.cpp

    src/external/imgui/widgets/console.cpp

    src/integrations/client/instance.cpp
    src/integrations/client/networking/engine.cpp

    # JavaScript scripting (V8 engine for client)
    src/integrations/client/scripting/module.cpp
)

# GUI (CEF-based)
list(APPEND FRAMEWORK_CLIENT_SRC
    src/gui/manager.cpp
    src/gui/clipboard.cpp
    src/gui/view.cpp
    src/gui/sdk.cpp
    src/gui/backend/view_d3d11.cpp
    src/gui/cef/app.cpp
    src/gui/cef/client.cpp
    src/gui/cef/render_handler.cpp
    src/gui/cef/life_span_handler.cpp
    src/gui/cef/load_handler.cpp
    src/gui/cef/display_handler.cpp
)

# Append the platform-dependent files to the related lists
if(WIN32)
    list(APPEND FRAMEWORK_CLIENT_SRC
        src/graphics/backend/d3d9.cpp
        src/graphics/backend/d3d11.cpp
        src/graphics/backend/d3d12.cpp
        src/utils/hooking/hook_function.cpp
        src/utils/hooking/hooking_patterns.cpp
        src/utils/hooking/hooking.cpp
        src/utils/memory.cpp
        src/utils/minidump.cpp
    )
endif()

# Additional libraries
find_package(OpenSSL REQUIRED)

# Build depending on the platform
add_library(Framework STATIC ${FRAMEWORK_SRC})

# Add services adapter to the project
add_subdirectory(src/services)

if(WIN32)
    # Build the loader and it's data
    add_library(FrameworkLoader
        STATIC
        src/utils/hashing.cpp
        src/launcher/project.cpp
        src/launcher/rgl_bypass.cpp
        src/utils/minidump.cpp
        src/launcher/loaders/exe_ldr.cpp
        src/external/steam/wrapper.cpp
        src/utils/hooking/hook_function.cpp
        src/utils/hooking/hooking_patterns.cpp
        src/utils/hooking/hooking.cpp
        src/utils/memory.cpp)
    target_link_options(FrameworkLoader PUBLIC /IGNORE:4254 /SAFESEH:NO /DYNAMICBASE:NO /LARGEADDRESSAWARE /FORCE:MULTIPLE /STACK:12000000,4096 /INCREMENTAL:NO /SUBSYSTEM:windows /LAST:.zdata /ENTRY:mainCRTStartup /MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/launcher.manifest)

    if(CMAKE_CL_64)
        target_link_options(FrameworkLoader PUBLIC /BASE:0x140000000)
    else()
        target_link_options(FrameworkLoader PUBLIC /BASE:0x400000)
    endif()

    add_library(FrameworkLoaderData
        SHARED
        src/launcher/data/tls.cpp
        src/launcher/data/seh_win32.cpp)

    # Build the client
    add_library(FrameworkClient STATIC ${FRAMEWORK_CLIENT_SRC})
endif()

if(LIBNODE_AVAILABLE)
    add_library(FrameworkServer STATIC ${FRAMEWORK_SERVER_SRC})
endif()

# Custom defines
target_compile_definitions(Framework PUBLIC MG_ENABLE_LOG=0)

# External libraries & other stuff
macro(link_shared_deps target_name)
    # Framework source directories (PUBLIC for dependent projects)
    target_include_directories(${target_name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/services>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )

    # Core vendor headers needed by all Framework targets (PUBLIC for API exposure)
    target_include_directories(${target_name} PUBLIC
        ${CMAKE_SOURCE_DIR}/vendors/glm
        ${CMAKE_SOURCE_DIR}/vendors/json/include
        ${CMAKE_SOURCE_DIR}/vendors/spdlog/include
        ${CMAKE_SOURCE_DIR}/vendors/fmt/include
        ${CMAKE_SOURCE_DIR}/vendors/fu2                    # function2 (used in network_peer.h)
        ${CMAKE_SOURCE_DIR}/vendors/mafianet/Source/include # Networking / MafiaNet (used in connection.h)
        ${CMAKE_SOURCE_DIR}/vendors/cxxopts                # Command-line parsing (used in integrations)
        ${CMAKE_SOURCE_DIR}/vendors                        # flecs, etc.
    )

    # Implementation-only vendor headers (PRIVATE - not part of public API)
    # Note: httplib include path is provided by the httplib target (uses split headers when HTTPLIB_COMPILE=ON)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/cppfs/source/cppfs/include
        ${CMAKE_SOURCE_DIR}/vendors/semver
        ${CMAKE_SOURCE_DIR}/vendors/sentry/include
        ${CMAKE_SOURCE_DIR}/vendors/curl/include
        ${CMAKE_SOURCE_DIR}/vendors/ftl/include
    )

    # Required libraries for apple systems
    if(APPLE)
        target_link_libraries(${target_name} "-framework CoreFoundation" "-framework Foundation" "-framework GSS" "-framework Security" "-framework CoreText" "-framework CoreGraphics" bsm)
    endif()

    # Windows-specific standard defines (target-scoped instead of global add_definitions)
    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE _USE_MATH_DEFINES NOMINMAX)
    endif()

    # Global libraries (v8/v8pp excluded - linked explicitly to scripting targets only)
    target_link_libraries(${target_name} MafiaNet glm spdlog cppfs nlohmann_json Sentry httplib OpenSSL::SSL OpenSSL::Crypto Curl semver Hash ftl)

    # Required libraries for windows
    if(WIN32)
        target_link_libraries(${target_name} StackWalker)
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/vendors/StackWalker/Main/StackWalker
        )
    endif()
endmacro()

# Node.js inspector support for script debugging (auto-enabled for Debug builds)
option(FW_NODE_INSPECTOR "Enable Node.js inspector for script debugging" OFF)

# FrameworkNodeEngine - Object library for node_engine.cpp (requires full libnode)
if(LIBNODE_AVAILABLE)
    # This is separate because it needs NODE_WANT_INTERNALS and libuv headers which
    # cause Windows SDK errno conflicts (EINVAL/ERANGE undefined) when applied to PCH.
    add_library(FrameworkNodeEngine OBJECT src/scripting/node_engine.cpp)

    # Apply the same shared dependencies as Framework
    target_include_directories(FrameworkNodeEngine PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/services>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
    target_include_directories(FrameworkNodeEngine PUBLIC
        ${CMAKE_SOURCE_DIR}/vendors/glm
        ${CMAKE_SOURCE_DIR}/vendors/json/include
        ${CMAKE_SOURCE_DIR}/vendors/spdlog/include
        ${CMAKE_SOURCE_DIR}/vendors/fmt/include
        ${CMAKE_SOURCE_DIR}/vendors/fu2
        ${CMAKE_SOURCE_DIR}/vendors
    )

    # Link to libnode for headers (includes V8 and Node.js headers)
    # Also link spdlog for logging used by node_engine.cpp
    target_link_libraries(FrameworkNodeEngine PUBLIC libnode v8pp spdlog)

    # Node.js internal APIs require NODE_WANT_INTERNALS
    target_compile_definitions(FrameworkNodeEngine PRIVATE NODE_WANT_INTERNALS=1)

    # Match Framework compile definitions and features
    target_compile_definitions(FrameworkNodeEngine PRIVATE MG_ENABLE_LOG=0)
    if(WIN32)
        target_compile_definitions(FrameworkNodeEngine PRIVATE _USE_MATH_DEFINES NOMINMAX)
    endif()
    target_compile_features(FrameworkNodeEngine PRIVATE cxx_std_20)

    # Node.js inspector: enabled via explicit option OR Debug builds
    target_compile_definitions(FrameworkNodeEngine PRIVATE
        $<$<OR:$<BOOL:${FW_NODE_INSPECTOR}>,$<CONFIG:Debug>>:FW_NODE_INSPECTOR>)
endif()

# FrameworkV8Engine - Object library for v8_engine.cpp
# Standalone V8 engine for client-side scripting (no Node.js dependency).
# Uses V8 symbols from the v8 target (which links libnode for the V8 runtime).
add_library(FrameworkV8Engine OBJECT src/scripting/v8_engine.cpp)

target_include_directories(FrameworkV8Engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/services>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_include_directories(FrameworkV8Engine PUBLIC
    ${CMAKE_SOURCE_DIR}/vendors/glm
    ${CMAKE_SOURCE_DIR}/vendors/json/include
    ${CMAKE_SOURCE_DIR}/vendors/spdlog/include
    ${CMAKE_SOURCE_DIR}/vendors/fmt/include
    ${CMAKE_SOURCE_DIR}/vendors/fu2
    ${CMAKE_SOURCE_DIR}/vendors
)
target_link_libraries(FrameworkV8Engine PUBLIC v8 v8pp spdlog)
target_compile_definitions(FrameworkV8Engine PRIVATE MG_ENABLE_LOG=0)
if(WIN32)
    target_compile_definitions(FrameworkV8Engine PRIVATE _USE_MATH_DEFINES NOMINMAX)
endif()
target_compile_features(FrameworkV8Engine PRIVATE cxx_std_20)

# Link the solutions based on the platform
link_shared_deps(Framework)
target_link_libraries(Framework v8 v8pp)
if(LIBNODE_AVAILABLE)
    target_link_libraries(Framework libnode)
endif()

# Include scripting engine objects in Framework
if(LIBNODE_AVAILABLE)
    target_sources(Framework PRIVATE $<TARGET_OBJECTS:FrameworkNodeEngine>)
endif()
target_sources(Framework PRIVATE $<TARGET_OBJECTS:FrameworkV8Engine>)

if(WIN32)
    link_shared_deps(FrameworkClient)
    target_link_libraries(FrameworkClient v8 v8pp)
    if(LIBNODE_AVAILABLE)
        target_link_libraries(FrameworkClient libnode)
    endif()
endif()

if(LIBNODE_AVAILABLE)
    link_shared_deps(FrameworkServer)
    target_link_libraries(FrameworkServer v8 v8pp MafiaHubServices libsig libnode)
    target_compile_definitions(FrameworkServer PRIVATE
        $<$<OR:$<BOOL:${FW_NODE_INSPECTOR}>,$<CONFIG:Debug>>:FW_NODE_INSPECTOR>)

    # FrameworkServer-specific includes
    target_include_directories(FrameworkServer PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/libsig
    )
endif()

# Platform-dependent post processing
if(WIN32)
    target_link_libraries(Framework ws2_32 dbghelp crypt32 winmm iphlpapi psapi userenv shlwapi)

    target_link_libraries(FrameworkClient MafiaHubServices DiscordSDK DearImGUI ImGuizmo)

    set(CLIENT_SHARED_LIBS minhook SteamSDK udis86)
    target_link_libraries(FrameworkClient ${CLIENT_SHARED_LIBS} ${FREETYPE_LIBRARY})

    # FrameworkClient-specific includes (Windows graphics, hooking, external services)
    target_include_directories(FrameworkClient PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/minhook/include
        ${CMAKE_SOURCE_DIR}/vendors/imgui
        ${CMAKE_SOURCE_DIR}/vendors/imguizmo
        ${CMAKE_SOURCE_DIR}/vendors/sdl2/include
        ${CMAKE_SOURCE_DIR}/vendors/steamworks/include
        ${CMAKE_SOURCE_DIR}/vendors/galaxy/Include
        ${CMAKE_SOURCE_DIR}/vendors/udis86
        ${CMAKE_SOURCE_DIR}/vendors/discord/src
        ${CMAKE_SOURCE_DIR}/vendors/freetype/include
        ${CMAKE_SOURCE_DIR}/vendors/optick/src
    )

    link_shared_deps(FrameworkLoader)
    target_link_libraries(FrameworkLoader ${CLIENT_SHARED_LIBS} MafiaHubServices FrameworkLoaderData ntdll sfd)
    target_link_libraries(FrameworkLoaderData udis86 minhook)
    target_compile_definitions(FrameworkLoaderData PRIVATE _USE_MATH_DEFINES NOMINMAX)
    target_link_options(FrameworkLoaderData PUBLIC /EXPORT:CoreRT_SetupSEHHandler /EXPORT:GetThreadLocalStorage /EXPORT:CoreSetExceptionOverride)

    # FrameworkLoader-specific includes
    target_include_directories(FrameworkLoader PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/minhook/include
        ${CMAKE_SOURCE_DIR}/vendors/ntdll
        ${CMAKE_SOURCE_DIR}/vendors/sfd
        ${CMAKE_SOURCE_DIR}/vendors/steamworks/include
        ${CMAKE_SOURCE_DIR}/vendors/udis86
    )

    # FrameworkLoaderData includes
    target_include_directories(FrameworkLoaderData PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/udis86
        ${CMAKE_SOURCE_DIR}/vendors/minhook/include
    )

    if(NOT CMAKE_CL_64)
        if(TARGET FrameworkServer)
            target_link_options(FrameworkServer PUBLIC "-SAFESEH:NO")
        endif()
    else()
        target_link_options(FrameworkClient PUBLIC "-SAFESEH:NO")
    endif()

    # CEF GUI integration (CEF target provides include dirs via INTERFACE)
    target_link_libraries(FrameworkClient CEF)
    target_include_directories(FrameworkClient PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/directxtk
    )

    # CEF subprocess executable
    add_executable(cef_subprocess
        src/gui/cef/subprocess_main.cpp
        src/gui/cef/renderer_app.cpp
    )
    target_link_libraries(cef_subprocess CEF)
    target_compile_definitions(cef_subprocess PRIVATE _USE_MATH_DEFINES NOMINMAX)
    set_target_properties(cef_subprocess PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(TARGET CEFCopyDlls)
        add_dependencies(FrameworkClient CEFCopyDlls)
        add_dependencies(cef_subprocess CEFCopyDlls)
    endif()

    # Ensure runtime DLLs are copied to bin directory for all client/server targets
    if(TARGET libnode_copy_dll)
        add_dependencies(FrameworkClient libnode_copy_dll)
        if(TARGET FrameworkServer)
            add_dependencies(FrameworkServer libnode_copy_dll)
        endif()
    endif()
    if(TARGET OpenSSLCopyStub)
        add_dependencies(FrameworkClient OpenSSLCopyStub)
        if(TARGET FrameworkServer)
            add_dependencies(FrameworkServer OpenSSLCopyStub)
        endif()
    endif()
endif()
