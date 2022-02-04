project(Lua C)
add_library(Lua53 STATIC
    src/lua53/lapi.c
    src/lua53/lauxlib.c
    src/lua53/lbaselib.c
    src/lua53/lbitlib.c
    src/lua53/lcode.c
    src/lua53/lcorolib.c
    src/lua53/lctype.c
    src/lua53/ldblib.c
    src/lua53/ldebug.c
    src/lua53/ldo.c
    src/lua53/ldump.c
    src/lua53/lfunc.c
    src/lua53/lgc.c
    src/lua53/linit.c
    src/lua53/liolib.c
    src/lua53/llex.c
    src/lua53/lmathlib.c
    src/lua53/lmem.c
    src/lua53/loadlib.c
    src/lua53/lobject.c
    src/lua53/lopcodes.c
    src/lua53/loslib.c
    src/lua53/lparser.c
    src/lua53/lstate.c
    src/lua53/lstring.c
    src/lua53/lstrlib.c
    src/lua53/ltable.c
    src/lua53/ltablib.c
    src/lua53/ltm.c
    src/lua53/lua.c
    src/lua53/lundump.c
    src/lua53/lvm.c
    src/lua53/lzio.c
)

target_include_directories(Lua53 PUBLIC src)
target_include_directories(Lua53 PRIVATE src/lua53)
