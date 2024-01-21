add_library(UltralightSDK STATIC dummy.cpp)
target_include_directories(UltralightSDK PUBLIC "include")

if (CMAKE_CL_64)
    target_link_directories(UltralightSDK PUBLIC "lib")
    add_custom_command(TARGET UltralightSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/Ultralight.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    add_custom_command(TARGET UltralightSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/UltralightCore.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    add_custom_command(TARGET UltralightSDK POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/WebCore.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(UltralightSDK PUBLIC Ultralight UltralightCore WebCore)
endif ()
