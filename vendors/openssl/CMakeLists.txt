add_library(OpenSSLCopyStub STATIC dummy.cpp)
target_include_directories(OpenSSLCopyStub PUBLIC "include")

if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_OPENSSL_VENDOR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86")
        set(_OPENSSL_VENDOR_CRYPTO_DLL "${CMAKE_CURRENT_SOURCE_DIR}/bin/x86/libcrypto-3.dll")
        set(_OPENSSL_VENDOR_SSL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/bin/x86/libssl-3.dll")
    else ()
        set(_OPENSSL_VENDOR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
        set(_OPENSSL_VENDOR_CRYPTO_DLL "${CMAKE_CURRENT_SOURCE_DIR}/bin/libcrypto-3-x64.dll")
        set(_OPENSSL_VENDOR_SSL_DLL "${CMAKE_CURRENT_SOURCE_DIR}/bin/libssl-3-x64.dll")
    endif ()

    target_link_directories(OpenSSLCopyStub PUBLIC "${_OPENSSL_VENDOR_LIB_DIR}")
    add_custom_command(TARGET OpenSSLCopyStub POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_OPENSSL_VENDOR_CRYPTO_DLL}" "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_OPENSSL_VENDOR_SSL_DLL}" "${CMAKE_BINARY_DIR}/bin"
            )
endif ()
