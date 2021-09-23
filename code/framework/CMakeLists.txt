# List the source files
set(FRAMEWORK_SRC
        src/logging/logger.cpp

        src/networking/entities/entity.cpp
        src/networking/entity_manager.cpp

        src/utils/delay_scope.cpp
        src/utils/config.cpp
        src/utils/interpolator.cpp
        src/utils/time.cpp
        src/utils/job_system.cpp

        src/external/discord/wrapper.cpp
        src/external/sentry/wrapper.cpp
        src/external/firebase/wrapper.cpp
        src/external/optick/wrapper.cpp
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

        src/networking/network_server.cpp
        src/networking/entity_streamer.cpp

        src/integrations/server/instance.cpp
        )

set(FRAMEWORK_CLIENT_SRC
        src/external/cef/application.cpp
        src/external/cef/client.cpp
        src/external/cef/render_handler.cpp
        src/external/cef/worker.hpp

        src/gui/web.cpp

        src/networking/network_client.cpp

        src/external/steam/wrapper.cpp

        src/integrations/client/instance.cpp
        )

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

    target_link_libraries(${target_name} slikenet glm spdlog cppfs nlohmann_json Sentry httplib ${OPENSSL_LIBRARIES} Firebase Curl OptickCore)
endmacro()

link_shared_deps(Framework)
link_shared_deps(FrameworkServer)
link_shared_deps(FrameworkClient)

target_link_libraries(FrameworkServer mongoose)
target_link_libraries(FrameworkClient DiscordSDK)

# NodeJS libraries
if (WIN32)
    target_link_directories(FrameworkServer PRIVATE "../../vendors/nodejs/lib/win32")
    file(GLOB NODEJS_LIBS "../../vendors/nodejs/lib/win32/*.lib")
elseif (APPLE)
    target_link_directories(FrameworkServer PRIVATE "../../vendors/nodejs/lib/osx")
    file(GLOB NODEJS_LIBS "../../vendors/nodejs/lib/osx/*.a")
elseif (UNIX)
    target_link_directories(FrameworkServer PRIVATE "../../vendors/nodejs/lib/linux")
    file(GLOB NODEJS_LIBS "../../vendors/nodejs/lib/linux/*.a")
endif ()
target_link_libraries(FrameworkServer ${NODEJS_LIBS})

# CEF libraries
if (WIN32)
    set(CEF_PLATFORM "windows32")
    target_link_directories(FrameworkClient PRIVATE "../../vendors/cef/lib/win_32" "../../vendors/cef/libcef_dll_wrapper")
    file(GLOB CEF_LIBS "../../vendors/cef/lib/win_32/*.lib" "../../vendors/cef/libcef_dll_wrapper/*.lib")
elseif (APPLE)
    set(CEF_PLATFORM "macosx64")
    target_link_directories(FrameworkClient PRIVATE "../../vendors/cef/lib/osx" "../../vendors/cef/libcef_dll_wrapper")
    file(GLOB CEF_LIBS "../../vendors/cef/lib/osx/*.a" "../../vendors/cef/lib/osx/*.framework" "../../vendors/cef/libcef_dll_wrapper/*.a")
elseif (UNIX)
    set(CEF_PLATFORM "linux32")
    target_link_directories(FrameworkClient PRIVATE "../../vendors/cef/lib/linux_32" "../../vendors/cef/libcef_dll_wrapper")
    file(GLOB CEF_LIBS "../../vendors/cef/lib/linux_32/*.a" "../../vendors/cef/libcef_dll_wrapper/*.a")
endif ()
target_link_libraries(FrameworkClient ${CEF_LIBS})

# OS specific libraries
if (WIN32)
    target_link_directories(Framework PUBLIC ${CMAKE_SOURCE_DIR}/vendors/openssl/lib)
    target_link_libraries(Framework ws2_32 dbghelp crypt32 winmm iphlpapi psapi userenv)
    target_link_libraries(FrameworkClient minhook SteamSDK)
    if (NOT CMAKE_CL_64)
        target_link_options(FrameworkServer PUBLIC "-SAFESEH:NO")
    endif ()
elseif (APPLE)
elseif (UNIX)
endif ()

# Custom defines
target_compile_definitions(Framework PUBLIC MG_ENABLE_LOG=0)
