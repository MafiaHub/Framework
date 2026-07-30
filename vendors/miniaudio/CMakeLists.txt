# The Framework root only enables CXX; miniaudio is pure C, so enable the language
# here the same way the other C-only vendors (udis86) do.
project(miniaudio C)

add_library(miniaudio STATIC miniaudio.c)

target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(miniaudio PRIVATE MINIAUDIO_IMPLEMENTATION)

# Voice moves raw PCM only. These are PUBLIC because they change what the header declares,
# so a consumer compiling with a different set would disagree about the ABI.
target_compile_definitions(miniaudio PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_WAV
    MA_NO_FLAC
    MA_NO_MP3
    MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH
    MA_NO_ENGINE
)

if(WIN32)
    # Windows audio backends are resolved through LoadLibrary at runtime, so nothing to link.
    target_compile_definitions(miniaudio PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
