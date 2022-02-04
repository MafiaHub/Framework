project(Lua C)
add_library(Lua54 STATIC
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
    src/lua54/lvm.c
    src/lua54/lzio.c
)

target_include_directories(Lua54 PUBLIC src)
target_include_directories(Lua54 PRIVATE src/lua54)
