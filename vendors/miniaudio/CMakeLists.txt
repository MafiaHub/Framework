# miniaudio - single-header audio capture/playback backend.
# Header-only for now: nothing defines MINIAUDIO_IMPLEMENTATION on this branch. The
# client milestone will compile the implementation exactly once inside FrameworkClient
# (audio_device.cpp), which does not exist yet.
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio INTERFACE ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
