add_library(Sentry STATIC dummy.cpp)
if(CMAKE_CL_64)
    target_link_directories(Sentry PUBLIC "lib/win_64")
    file(GLOB SENTRY_LIBS "lib/win_64/*.lib")
    add_custom_command(TARGET Sentry POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/win_64/crashpad_handler.exe" "${CMAKE_BINARY_DIR}/bin/"
            )
elseif(WIN32)
    target_link_directories(Sentry PUBLIC "lib/win_32")
    file(GLOB SENTRY_LIBS "lib/win_32/*.lib")
    add_custom_command(TARGET Sentry POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/win_32/crashpad_handler.exe" "${CMAKE_BINARY_DIR}/bin/"
            )
elseif(APPLE)
    target_link_directories(Sentry PUBLIC "lib/osx")
    file(GLOB SENTRY_LIBS "lib/osx/*.a")
    add_custom_command(TARGET Sentry POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/osx/crashpad_handler" "${CMAKE_BINARY_DIR}/"
            )
else()
    target_link_directories(Sentry PUBLIC "lib/linux")
    file(GLOB SENTRY_LIBS "lib/linux/*.a")
endif()
target_link_libraries(Sentry PUBLIC ${SENTRY_LIBS})
target_include_directories(Sentry PUBLIC "include")
