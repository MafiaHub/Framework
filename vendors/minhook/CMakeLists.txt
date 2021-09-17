project(minhook-stuff)
add_library(minhook STATIC buffer.c hde32.c hde64.c hook.c trampoline.c)
target_include_directories(minhook PUBLIC ".")
