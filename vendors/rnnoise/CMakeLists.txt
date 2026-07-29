# RNNoise - neural-network noise suppression used by RakVoice.
file(GLOB RNNOISE_SRC "src/*.c")
list(FILTER RNNOISE_SRC EXCLUDE REGEX "(dump_features|write_weights|dump_rnnoise_tables)\\.c$")

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
