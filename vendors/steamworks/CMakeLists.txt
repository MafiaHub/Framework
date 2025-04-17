add_library(SteamSDK STATIC dummy.cpp)
target_include_directories(SteamSDK PUBLIC "include")

if (CMAKE_CL_64)
    target_link_directories(SteamSDK PUBLIC "lib/win64")
    add_custom_command(TARGET SteamSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/win64/fw_steam_api64.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(SteamSDK PUBLIC fw_steam_api64)
elseif (WIN32)
    target_link_directories(SteamSDK PUBLIC "lib/win32")
    add_custom_command(TARGET SteamSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/win32/fw_steam_api.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(SteamSDK PUBLIC fw_steam_api)
endif ()
