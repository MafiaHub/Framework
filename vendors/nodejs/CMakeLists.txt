add_library(NodeJS STATIC dummy.cpp)

# Build nghttp2
if (UNIX)
    add_subdirectory(nghttp2)
endif ()

if (CMAKE_CL_64)
    target_link_directories(NodeJS INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/lib/win64")
    file(GLOB NODEJS_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/win64/*.lib")
    set(NODEJS_ARCH "win64")
    add_custom_command(TARGET NodeJS POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/win64/libnode.dll" "${CMAKE_BINARY_DIR}/bin/"
            )
elseif (WIN32)
    target_link_directories(NodeJS INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/lib/win32")
    file(GLOB NODEJS_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/win32/*.lib")
    set(NODEJS_ARCH "win32")
    add_custom_command(TARGET NodeJS POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/lib/win32/libnode.dll" "${CMAKE_BINARY_DIR}/bin/"
            )
elseif (APPLE)
    target_link_directories(NodeJS INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/lib/darwin")
    file(GLOB NODEJS_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/darwin/*.a")
    set(NODEJS_ARCH "darwin")
elseif (UNIX)
    target_link_directories(NodeJS INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/lib/linux")
    target_link_libraries(NodeJS INTERFACE nghttp2)
    file(GLOB NODEJS_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/linux/*.a")
    set(NODEJS_ARCH "linux")
endif ()

target_include_directories(NodeJS PUBLIC "include")

if (WIN32)
  target_link_libraries(NodeJS libnode.lib dbghelp.lib winmm.lib shlwapi.lib)
else()
  target_link_libraries(NodeJS node.102)
endif ()

message(STATUS "NodeJS libraries: ${NODEJS_LIBS}")

# TODO shit code
if (WIN32)
    set(V8_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/${NODEJS_ARCH}/v8_libbase.lib" PARENT_SCOPE)
    set(V8_LIBPLATFORM "${CMAKE_CURRENT_SOURCE_DIR}/lib/${NODEJS_ARCH}/v8_libplatform.lib" PARENT_SCOPE)
else ()
    set(V8_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/${NODEJS_ARCH}/v8_libbase.a" PARENT_SCOPE)
    set(V8_LIBPLATFORM "${CMAKE_CURRENT_SOURCE_DIR}/lib/${NODEJS_ARCH}/v8_libplatform.a" PARENT_SCOPE)
endif ()
set(V8_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include" PARENT_SCOPE)
