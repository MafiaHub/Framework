add_library(SteamSDK STATIC dummy.cpp)
target_include_directories(SteamSDK PUBLIC "include")

if (WIN32)
    target_link_directories(SteamSDK PUBLIC "lib/win32")
    add_custom_command(TARGET SteamSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/win32/steam_api.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(SteamSDK PUBLIC steam_api)
endif ()
