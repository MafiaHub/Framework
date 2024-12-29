add_library(OpenSSLCopyStub STATIC dummy.cpp)
target_include_directories(OpenSSLCopyStub PUBLIC "include")

if (WIN32)
    target_link_directories(OpenSSLCopyStub PUBLIC "lib")
    add_custom_command(TARGET OpenSSLCopyStub POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/libcrypto-3-x64.dll" "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/libssl-3-x64.dll" "${CMAKE_BINARY_DIR}/bin"
            )
endif ()
