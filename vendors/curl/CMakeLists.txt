add_library(Curl INTERFACE)

if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_CURL_VENDOR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86")
    else ()
        set(_CURL_VENDOR_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    endif ()

    target_link_directories(Curl INTERFACE "${_CURL_VENDOR_LIB_DIR}")
    target_link_libraries(Curl INTERFACE libcurl)
else ()
    target_link_libraries(Curl INTERFACE curl)
endif ()
