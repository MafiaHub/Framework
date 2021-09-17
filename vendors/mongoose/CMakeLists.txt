project(mongoose C)
add_library(mongoose STATIC mongoose.c)

if (WIN32)
    target_link_libraries(mongoose ws2_32)
endif ()

target_compile_definitions(mongoose PUBLIC MG_ENABLE_LOG=0 MG_ENABLE_DIRECTORY_LISTING=0 MG_ENABLE_FS=1)
