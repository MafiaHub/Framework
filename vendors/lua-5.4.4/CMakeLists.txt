project(Lua C)

set(LUA_FILES
    src/lua54/lapi.c
    src/lua54/lauxlib.c
    src/lua54/lbaselib.c
    src/lua54/lcode.c
    src/lua54/lcorolib.c
    src/lua54/lctype.c
    src/lua54/ldblib.c
    src/lua54/ldebug.c
    src/lua54/ldo.c
    src/lua54/ldump.c
    src/lua54/lfunc.c
    src/lua54/lgc.c
    src/lua54/linit.c
    src/lua54/liolib.c
    src/lua54/llex.c
    src/lua54/lmathlib.c
    src/lua54/lmem.c
    src/lua54/loadlib.c
    src/lua54/lobject.c
    src/lua54/lopcodes.c
    src/lua54/loslib.c
    src/lua54/lparser.c
    src/lua54/lstate.c
    src/lua54/lstring.c
    src/lua54/lstrlib.c
    src/lua54/ltable.c
    src/lua54/ltablib.c
    src/lua54/ltm.c
    src/lua54/lua.c
    src/lua54/lundump.c
    src/lua54/lutf8lib.c
    src/lua54/lvm.c
    src/lua54/lzio.c
)

file(GLOB LUA_HEADERS
    src/lua54/*.h
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The library source files (no lua.c for static/shared libs)
set(LUA_LIB_FILES ${LUA_FILES})
list(REMOVE_ITEM LUA_LIB_FILES src/lua54/lua.c)

# Static Library
add_library(lua54_static STATIC ${LUA_LIB_FILES})
add_custom_command(TARGET lua54_static POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lua54_static>/include"
    COMMAND ${CMAKE_COMMAND} -E copy ${LUA_HEADERS} "$<TARGET_FILE_DIR:lua54_static>/include"
    COMMENT "Copying Lua headers to output folder"
)
set_target_properties(lua54_static PROPERTIES OUTPUT_NAME "lua-54")

# Shared Library
add_library(lua54_shared SHARED ${LUA_LIB_FILES})
set_target_properties(lua54_shared PROPERTIES OUTPUT_NAME "lua")

# Executable
add_executable(lua54_exe ${LUA_FILES})
set_target_properties(lua54_exe PROPERTIES OUTPUT_NAME "lua54")
