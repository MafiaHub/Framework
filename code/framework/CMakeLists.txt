# List the source files
set(FRAMEWORK_SRC
        src/logging/logger.cpp

        src/utils/delay_scope.cpp
        src/utils/config.cpp
        src/utils/interpolator.cpp
        src/utils/time.cpp
        src/utils/job_system.cpp
        src/utils/command_processor.cpp
        src/utils/hashing.cpp

        src/external/discord/wrapper.cpp
        src/external/sentry/wrapper.cpp
        src/external/firebase/wrapper.cpp
        src/external/optick/wrapper.cpp

        src/world/engine.cpp

        src/networking/network_peer.cpp
        )

set(FRAMEWORK_SERVER_SRC
        src/scripting/sdk.cpp
        src/scripting/engine.cpp
        src/scripting/resource.cpp
        src/scripting/resource_manager.cpp

        src/scripting/builtins/factory.cpp

        src/scripting/v8_helpers/argstack.cpp
        src/scripting/v8_helpers/v8_class.cpp
        src/scripting/v8_helpers/v8_module.cpp
        src/scripting/v8_helpers/v8_source_location.cpp
        src/scripting/v8_helpers/v8_try_catch.cpp

        src/http/webserver.cpp

        src/services/masterlist.cpp
        src/services/user.cpp

        src/networking/network_server.cpp

        src/world/server.cpp

        src/integrations/server/instance.cpp
        src/integrations/server/masterlist.cpp
        src/integrations/server/networking/engine.cpp
        )

set(FRAMEWORK_CLIENT_SRC
        src/external/cef/application.cpp
        src/external/cef/client.cpp
        src/external/cef/render_handler.cpp
        src/external/cef/worker.hpp

        src/graphics/renderer.cpp
        src/gui/api/cef.cpp

        src/networking/network_client.cpp

        src/external/steam/wrapper.cpp

        src/integrations/client/instance.cpp
    )

# Append the platform-dependent files to the related lists
if(WIN32)
    list(APPEND FRAMEWORK_CLIENT_SRC 
        src/graphics/backend/d3d9.cpp
        src/graphics/backend/d3d11.cpp
        src/utils/hooking/hook_function.cpp
        src/utils/hooking/hooking_patterns.cpp
        src/utils/hooking/hooking.cpp
        src/utils/memory.cpp
    )
    add_library(FrameworkLoader
        STATIC
        src/utils/hashing.cpp
        src/launcher/project.cpp
        src/launcher/loaders/exe_ldr.cpp
        src/external/steam/wrapper.cpp
        src/utils/hooking/hook_function.cpp
        src/utils/hooking/hooking_patterns.cpp
        src/utils/hooking/hooking.cpp
        src/utils/memory.cpp
    )
    target_link_options(FrameworkLoader PUBLIC /IGNORE:4254 /SAFESEH:NO /DYNAMICBASE:NO /LARGEADDRESSAWARE /SUBSYSTEM:windows /SECTIONLAYOUT:@${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/SectionLayout.txt)

    if (CMAKE_CL_64)
        target_link_options(FrameworkLoader PUBLIC /BASE:0x140000000)
    else()
        target_link_options(FrameworkLoader PUBLIC /BASE:0x400000)
    endif()

    add_library(FrameworkLoaderData
        SHARED 
        src/launcher/data/tls.cpp
        src/launcher/data/seh_win32.cpp)
endif()

# Append the files to the target static library
add_library(Framework STATIC ${FRAMEWORK_SRC})
add_library(FrameworkClient STATIC ${FRAMEWORK_CLIENT_SRC})
add_library(FrameworkServer STATIC ${FRAMEWORK_SERVER_SRC})

find_package(OpenSSL REQUIRED)

# External libraries & other stuff
macro(link_shared_deps target_name)
    target_include_directories(${target_name} PUBLIC "src")

    # Required libraries for apple systems
    if(APPLE)
        target_link_libraries(${target_name} "-framework CoreFoundation" "-framework Foundation" "-framework GSS" "-framework Security" "-framework CoreText" "-framework CoreGraphics" bsm)
    endif()

    target_link_libraries(${target_name} slikenet glm spdlog cppfs nlohmann_json Sentry httplib ${OPENSSL_LIBRARIES} Firebase Curl OptickCore flecs_static)
endmacro()

link_shared_deps(Framework)
link_shared_deps(FrameworkServer)
link_shared_deps(FrameworkClient)

target_link_libraries(FrameworkServer mongoose NodeJS libsig)
target_link_libraries(FrameworkClient DiscordSDK CefTarget)

# OS specific libraries
if (WIN32)
    target_link_directories(Framework PUBLIC ${CMAKE_SOURCE_DIR}/vendors/openssl/lib)
    target_link_libraries(Framework ws2_32 dbghelp crypt32 winmm iphlpapi psapi userenv)

    set(CLIENT_SHARED_LIBS minhook SteamSDK udis86)
    target_link_libraries(FrameworkClient ${CLIENT_SHARED_LIBS})

    link_shared_deps(FrameworkLoader)
    target_link_libraries(FrameworkLoader ${CLIENT_SHARED_LIBS} FrameworkLoaderData ntdll)
    target_link_libraries(FrameworkLoaderData udis86 minhook)
    target_link_options(FrameworkLoaderData PUBLIC /EXPORT:CoreRT_SetupSEHHandler /EXPORT:GetThreadLocalStorage)
    
    if (NOT CMAKE_CL_64)
        target_link_options(FrameworkServer PUBLIC "-SAFESEH:NO")
    else ()
        target_link_libraries(FrameworkClient Ultralight)
        target_link_options(FrameworkClient PUBLIC "-SAFESEH:NO")
    endif ()
else ()
    target_link_libraries(FrameworkClient Ultralight)
endif ()

# Custom defines
target_compile_definitions(Framework PUBLIC MG_ENABLE_LOG=0)
IF(WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-DNOMINMAX)
ENDIF()
