cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(Framework CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FW_STANDALONE true)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

if(FW_STANDALONE)
    include(FrameworkSetup)
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/vendors)

# Load the global headers one by one (a few ones have relative include path, so we are forced to do so)
include_directories(
    "${PROJECT_SOURCE_DIR}/vendors/mongoose"
    "${PROJECT_SOURCE_DIR}/vendors/nodejs/include"
    "${PROJECT_SOURCE_DIR}/vendors/glm"
    "${PROJECT_SOURCE_DIR}/vendors/minhook/include"
    "${PROJECT_SOURCE_DIR}/vendors/firebase/include"
    "${PROJECT_SOURCE_DIR}/vendors/cppfs/source/cppfs/include"
    "${PROJECT_SOURCE_DIR}/vendors/json/include"
    "${PROJECT_SOURCE_DIR}/vendors/slikenet/Source"
    "${PROJECT_SOURCE_DIR}/vendors/ntdll"
    "${PROJECT_SOURCE_DIR}/vendors/semver"
    "${PROJECT_SOURCE_DIR}/vendors/imgui"
    "${PROJECT_SOURCE_DIR}/vendors/lua/src"
    "${PROJECT_SOURCE_DIR}/vendors/sdl2/include"
    "${PROJECT_SOURCE_DIR}/vendors/spdlog/include"
    "${PROJECT_SOURCE_DIR}/vendors/optick/src"
    "${PROJECT_SOURCE_DIR}/vendors/steamworks/include"
    "${PROJECT_SOURCE_DIR}/vendors/galaxy/Include"
    "${PROJECT_SOURCE_DIR}/vendors/udis86"
    "${PROJECT_SOURCE_DIR}/vendors/httplib"
    "${PROJECT_SOURCE_DIR}/vendors/lua-5.2.1/src"
    "${PROJECT_SOURCE_DIR}/vendors/lua-5.3.6/src"
    "${PROJECT_SOURCE_DIR}/vendors/lua-5.4.4/src"
    "${PROJECT_SOURCE_DIR}/vendors/discord/src"
    "${PROJECT_SOURCE_DIR}/vendors/sentry/include"
    "${PROJECT_SOURCE_DIR}/vendors/curl/include"
    "${PROJECT_SOURCE_DIR}/vendors/v8pp"
    "${PROJECT_SOURCE_DIR}/vendors/fmt/include"
    "${PROJECT_SOURCE_DIR}/vendors/libsig"
    "${PROJECT_SOURCE_DIR}/vendors/sfd"
    "${PROJECT_SOURCE_DIR}/vendors/cxxopts"
    "${PROJECT_SOURCE_DIR}/vendors/fu2"
    "${PROJECT_SOURCE_DIR}/vendors/freetype/include"
    "${PROJECT_SOURCE_DIR}/vendors/StackWalker/Main/StackWalker"
    "${PROJECT_SOURCE_DIR}/vendors/ultralight/include"
    "${PROJECT_SOURCE_DIR}/vendors/directxtk"
    "${PROJECT_SOURCE_DIR}/vendors/ultralight/include"
    "${PROJECT_SOURCE_DIR}/vendors"
    "${PROJECT_SOURCE_DIR}/code/framework/src/"
    "${PROJECT_SOURCE_DIR}/code/framework/"
)

# Include all Framework dependencies
add_subdirectory(${PROJECT_SOURCE_DIR}/code)
