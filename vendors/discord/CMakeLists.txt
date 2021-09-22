file(GLOB DISCORD_FILES "src/*.cpp")
add_library(DiscordSDK STATIC ${DISCORD_FILES})
target_include_directories(DiscordSDK PUBLIC "src")

if (WIN32)
    target_link_directories(DiscordSDK PUBLIC "lib/x86")
    add_custom_command(TARGET DiscordSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86/discord_game_sdk.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(DiscordSDK PUBLIC discord_game_sdk)
elseif (APPLE)
    target_link_directories(DiscordSDK PUBLIC "lib/x86_64")
    add_custom_command(TARGET DiscordSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86_64/discord_game_sdk.dylib" "${CMAKE_BINARY_DIR}/"
            )
    target_link_libraries(DiscordSDK ${CMAKE_CURRENT_SOURCE_DIR}/lib/x86_64/discord_game_sdk.dylib)
endif ()
