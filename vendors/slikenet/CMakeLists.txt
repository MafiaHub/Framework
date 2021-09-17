file(GLOB_RECURSE SLIKENET_SRC "Source/*.cpp")
add_library(slikenet STATIC ${SLIKENET_SRC})
target_include_directories(slikenet PRIVATE "Source")
