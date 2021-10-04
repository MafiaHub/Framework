project(udis C)
add_library(udis86 STATIC decode.c itab.c syn.c syn-att.c syn-intel.c udis86.c)
target_include_directories(udis86 PRIVATE ".")
target_compile_definitions(udis86 PUBLIC "__UD_STANDALONE__")
