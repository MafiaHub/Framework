# miniaudio - single-header audio capture/playback backend.
# The implementation is compiled once inside FrameworkClient
# (audio_device.cpp defines MINIAUDIO_IMPLEMENTATION).
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio INTERFACE ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
