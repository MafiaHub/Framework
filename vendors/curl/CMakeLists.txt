add_library(Curl INTERFACE)

if (WIN32)
    target_link_directories(Curl INTERFACE "lib")
    target_link_libraries(Curl INTERFACE libcurl)
else ()
    target_link_libraries(Curl INTERFACE curl)
endif ()
