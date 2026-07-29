# RNNoise - neural-network noise suppression used by RakVoice.
#
# Explicit source list, transcribed from upstream Makefile.am's RNNOISE_SOURCES.
# Deliberately NOT a glob: src/ also holds standalone tools with their own main()
# (dump_features.c, write_weights.c, dump_rnnoise_tables.c) and an alternative
# weights blob, and a glob pulls whichever of those upstream adds next straight
# into the library. The x86 RTCD sources (src/x86/*.c) are conditional upstream on
# RNN_ENABLE_X86_RTCD and are not built here.
set(RNNOISE_SRC
    "src/denoise.c"
    "src/rnn.c"
    "src/pitch.c"
    "src/kiss_fft.c"
    "src/celt_lpc.c"
    "src/nnet.c"
    "src/nnet_default.c"
    "src/parse_lpcnet_weights.c"
    "src/rnnoise_data.c"
    "src/rnnoise_tables.c")

add_library(rnnoise STATIC ${RNNOISE_SRC})
target_include_directories(rnnoise PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(rnnoise PRIVATE /w)
else()
    target_compile_options(rnnoise PRIVATE -w)
endif()
