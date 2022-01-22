project(Lua C)
add_library(Lua52 STATIC
    src/lua52/lapi.c
    src/lua52/lauxlib.c
    src/lua52/lbaselib.c
    src/lua52/lbitlib.c
    src/lua52/lcode.c
    src/lua52/lcorolib.c
    src/lua52/lctype.c
    src/lua52/ldblib.c
    src/lua52/ldebug.c
    src/lua52/ldo.c
    src/lua52/ldump.c
    src/lua52/lfunc.c
    src/lua52/lgc.c
    src/lua52/linit.c
    src/lua52/liolib.c
    src/lua52/llex.c
    src/lua52/lmathlib.c
    src/lua52/lmem.c
    src/lua52/loadlib.c
    src/lua52/lobject.c
    src/lua52/lopcodes.c
    src/lua52/loslib.c
    src/lua52/lparser.c
    src/lua52/lstate.c
    src/lua52/lstring.c
    src/lua52/lstrlib.c
    src/lua52/ltable.c
    src/lua52/ltablib.c
    src/lua52/ltm.c
    src/lua52/lua.c
    src/lua52/lundump.c
    src/lua52/lvm.c
    src/lua52/lzio.c
)

target_include_directories(Lua52 PUBLIC src)
target_include_directories(Lua52 PRIVATE src/lua52)
