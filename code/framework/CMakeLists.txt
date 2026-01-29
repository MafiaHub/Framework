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

    src/world/engine.cpp
    src/world/server.cpp
    src/world/client.cpp
    src/world/modules/modules_impl.cpp

    src/networking/network_peer.cpp
    src/networking/errors.cpp

    src/scripting/engine.cpp
    src/scripting/client_engine.cpp
    src/scripting/types/events.cpp

    src/integrations/client/scripting/module.cpp

    src/scripting/resource/resource.cpp
    src/scripting/resource/resource_manifest.cpp
    src/scripting/resource/dependency_graph.cpp
    src/scripting/resource/environment_sandbox.cpp
    src/scripting/resource/resource_manager.cpp

    # JavaScript scripting (V8/Node.js)
    src/scripting/js/engine.cpp
    src/scripting/js/v8_engine.cpp
    src/scripting/js/builtins/vector2.cpp
    src/scripting/js/builtins/vector3.cpp
    src/scripting/js/builtins/vector4.cpp
    src/scripting/js/builtins/quaternion.cpp
    src/scripting/js/builtins/color.cpp
    src/scripting/js/resource/package_manifest.cpp
    src/scripting/js/resource/js_resource.cpp
    src/scripting/js/resource/js_resource_manager.cpp

    src/jobs/job_system.cpp
    src/jobs/io_tasks.cpp
)

set(FRAMEWORK_SERVER_SRC
    src/scripting/server_engine.cpp

    src/http/webserver.cpp

    src/networking/network_server.cpp

    src/integrations/server/instance.cpp
    src/integrations/server/networking/engine.cpp
    src/integrations/server/scripting/module.cpp
    src/integrations/server/scripting/builtins/entity.cpp
    src/integrations/server/scripting/builtins/events_lua.cpp

    # JavaScript scripting (Node.js engine for server)
    src/scripting/js/node_engine.cpp
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
)

# GUI (Ultralight) only available for 64-bit builds
if(CMAKE_CL_64)
    list(APPEND FRAMEWORK_CLIENT_SRC
        src/gui/manager.cpp
        src/gui/clipboard.cpp
        src/gui/view.cpp
        src/gui/sdk.cpp
        src/gui/backend/renderer_d3d11.cpp
        src/gui/backend/view_d3d11.cpp
    )
endif()

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
        src/utils/minidump.cpp
        src/launcher/loaders/exe_ldr.cpp
        src/external/steam/wrapper.cpp
        src/utils/hooking/hook_function.cpp
        src/utils/hooking/hooking_patterns.cpp
        src/utils/hooking/hooking.cpp
        src/utils/memory.cpp)
    target_link_options(FrameworkLoader PUBLIC /IGNORE:4254 /SAFESEH:NO /DYNAMICBASE:NO /LARGEADDRESSAWARE /FORCE:MULTIPLE /STACK:12000000,4096 /INCREMENTAL:NO /SUBSYSTEM:windows /LAST:.zdata /ENTRY:mainCRTStartup)

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

add_library(FrameworkServer STATIC ${FRAMEWORK_SERVER_SRC})

# Precompiled Headers (PCH) for faster builds
# Framework base PCH - stable, frequently used headers
# Using PUBLIC so child targets can inherit via INTERFACE_PRECOMPILE_HEADERS
target_precompile_headers(Framework PUBLIC
    # STL (stable, frequently used)
    <memory>
    <string>
    <vector>
    <unordered_map>
    <functional>

    # Stable vendor headers
    <spdlog/spdlog.h>
    <glm/glm.hpp>
    <glm/gtc/quaternion.hpp>
    <nlohmann/json.hpp>
)

# FrameworkServer PCH - reuses Framework PCH + adds scripting/ECS headers
target_precompile_headers(FrameworkServer PRIVATE
    "$<TARGET_PROPERTY:Framework,INTERFACE_PRECOMPILE_HEADERS>"
    <sol/sol.hpp>
    <flecs/flecs.h>
)

# FrameworkClient PCH (Windows only)
if(WIN32)
    target_precompile_headers(FrameworkClient PRIVATE
        "$<TARGET_PROPERTY:Framework,INTERFACE_PRECOMPILE_HEADERS>"
        <flecs/flecs.h>
    )
endif()

# Custom defines
target_compile_definitions(Framework PUBLIC MG_ENABLE_LOG=0)

IF(WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-DNOMINMAX)
ENDIF()

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
        ${CMAKE_SOURCE_DIR}/vendors/sol                    # Lua bindings (used in scripting API)
        ${CMAKE_SOURCE_DIR}/vendors/lua-5.4.4/src/lua54    # Lua headers (required by sol)
        ${CMAKE_SOURCE_DIR}/vendors/fu2                    # function2 (used in world/modules/base.hpp)
        ${CMAKE_SOURCE_DIR}/vendors/slikenet/Source        # Networking (used in messages.h)
        ${CMAKE_SOURCE_DIR}/vendors/cxxopts                # Command-line parsing (used in integrations)
        ${CMAKE_SOURCE_DIR}/vendors                        # flecs, etc.
    )

    # Implementation-only vendor headers (PRIVATE - not part of public API)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/cppfs/source/cppfs/include
        ${CMAKE_SOURCE_DIR}/vendors/semver
        ${CMAKE_SOURCE_DIR}/vendors/httplib
        ${CMAKE_SOURCE_DIR}/vendors/sentry/include
        ${CMAKE_SOURCE_DIR}/vendors/curl/include
        ${CMAKE_SOURCE_DIR}/vendors/ftl/include
    )

    # Required libraries for apple systems
    if(APPLE)
        target_link_libraries(${target_name} "-framework CoreFoundation" "-framework Foundation" "-framework GSS" "-framework Security" "-framework CoreText" "-framework CoreGraphics" bsm)
    endif()

    # Global libraries
    target_link_libraries(${target_name} slikenet glm spdlog cppfs nlohmann_json Sentry httplib ${OPENSSL_LIBRARIES} Curl flecs_static semver Hash ftl v8)

    # Required libraries for windows
    if(WIN32)
        target_link_libraries(${target_name} StackWalker)
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/vendors/StackWalker/Main/StackWalker
        )
    endif()
endmacro()

# Link the solutions based on the platform
link_shared_deps(Framework)

if(WIN32)
    link_shared_deps(FrameworkClient)
endif()

link_shared_deps(FrameworkServer)
target_link_libraries(FrameworkServer MafiaHubServices libsig lua54_static libnode)

# FrameworkServer-specific includes
target_include_directories(FrameworkServer PRIVATE
    ${CMAKE_SOURCE_DIR}/vendors/libsig
)

# Platform-dependent post processing
if(WIN32)
    target_link_directories(Framework PUBLIC ${CMAKE_SOURCE_DIR}/vendors/openssl/lib)
    target_link_libraries(Framework ws2_32 dbghelp crypt32 winmm iphlpapi psapi userenv shlwapi)

    target_link_libraries(FrameworkClient MafiaHubServices DiscordSDK DearImGUI)

    set(CLIENT_SHARED_LIBS minhook SteamSDK udis86)
    target_link_libraries(FrameworkClient ${CLIENT_SHARED_LIBS} ${FREETYPE_LIBRARY})

    # FrameworkClient-specific includes (Windows graphics, hooking, external services)
    target_include_directories(FrameworkClient PRIVATE
        ${CMAKE_SOURCE_DIR}/vendors/minhook/include
        ${CMAKE_SOURCE_DIR}/vendors/imgui
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
        target_link_options(FrameworkServer PUBLIC "-SAFESEH:NO")
    else()
        target_link_libraries(FrameworkClient UltralightSDK JavaScriptCorePP)
        target_link_options(FrameworkClient PUBLIC "-SAFESEH:NO")

        # Ultralight GUI includes (64-bit only)
        target_include_directories(FrameworkClient PRIVATE
            ${CMAKE_SOURCE_DIR}/vendors/ultralight/include
            ${CMAKE_SOURCE_DIR}/vendors/directxtk
            ${CMAKE_SOURCE_DIR}/vendors/javascriptcorepp/include
        )
    endif()
endif()
