file(GLOB DISCORD_FILES "src/*.cpp")
add_library(DiscordSDK STATIC ${DISCORD_FILES})
target_include_directories(DiscordSDK PRIVATE "src")
fw_sdk_export_includes(DiscordSDK discord "${CMAKE_CURRENT_SOURCE_DIR}/src")
fw_sdk_install_headers(discord "${CMAKE_CURRENT_SOURCE_DIR}/src")

if (CMAKE_CL_64)
    set(_discord_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86_64")
elseif (WIN32)
    set(_discord_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86")
endif ()
if (WIN32)
    fw_sdk_export_link_directories(DiscordSDK "${_discord_lib_dir}")
    add_custom_command(TARGET DiscordSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${_discord_lib_dir}/discord_game_sdk.dll" "${CMAKE_BINARY_DIR}/bin/"
            )
    target_link_libraries(DiscordSDK PUBLIC discord_game_sdk)
    install(FILES "${_discord_lib_dir}/discord_game_sdk.lib" DESTINATION "${FW_SDK_LIB_DIR}")
    fw_sdk_runtime(CLIENT "${_discord_lib_dir}/discord_game_sdk.dll")
elseif (APPLE)
    target_link_directories(DiscordSDK PUBLIC "lib/x86_64")
    add_custom_command(TARGET DiscordSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86_64/discord_game_sdk.dylib" "${CMAKE_BINARY_DIR}/"
            )
    target_link_libraries(DiscordSDK ${CMAKE_CURRENT_SOURCE_DIR}/lib/x86_64/discord_game_sdk.dylib)
endif ()
