# MafiaNet - MafiaHub's networking engine (fork of RakNet/SLikeNet).
# Vendored, pinned to tag v0.8.0 (see VERSION.txt). Built as a static library.
#
# Public headers live under Source/include and are consumed with the
# "mafianet/" prefix, e.g. #include <mafianet/peerinterface.h>.

file(GLOB MAFIANET_SRC
    "Source/src/*.cpp"
    "Source/src/crypto/*.cpp"
)
# Guard against stray hidden/temp files (e.g. ".!<pid>!foo.cpp") being picked up.
list(FILTER MAFIANET_SRC EXCLUDE REGEX "/\\.")

add_library(MafiaNet STATIC ${MAFIANET_SRC})

target_include_directories(MafiaNet PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/include")

# OpenSSL is required by MafiaNet's crypto module and secure handshake. Reuse the
# targets already provided by the surrounding build (httplib pulls OpenSSL in
# before this directory is configured); fall back to find_package otherwise.
if(NOT TARGET OpenSSL::SSL)
    find_package(OpenSSL REQUIRED)
endif()
find_package(Threads REQUIRED)

target_link_libraries(MafiaNet PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

if(WIN32)
    target_link_libraries(MafiaNet PUBLIC ws2_32)
    target_compile_definitions(MafiaNet PUBLIC _MAFIANET_LIB)
endif()

# Suppress third-party warnings (this is vendored code).
if(MSVC)
    target_compile_options(MafiaNet PRIVATE /w)
else()
    target_compile_options(MafiaNet PRIVATE -w)
endif()
