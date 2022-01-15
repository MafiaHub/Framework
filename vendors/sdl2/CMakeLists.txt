add_library(SDL STATIC dummy.cpp)
target_include_directories(SDL PUBLIC "include")
target_include_directories(SDL PRIVATE "../sdl2/include")

if (CMAKE_CL_64)
    target_link_directories(SDL PUBLIC "lib/x64")
    add_custom_command(TARGET SDL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/x64/SDL2.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(SDL PUBLIC SDL2)
elseif (WIN32)
    target_link_directories(SDL PUBLIC "lib/x86")
    add_custom_command(TARGET SDL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86/SDL2.dll" "${CMAKE_BINARY_DIR}/bin"
            )
    target_link_libraries(SDL PUBLIC SDL2)
endif ()
