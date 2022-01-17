project(semver C)
add_library(semver STATIC semver.c)
target_include_directories(semver PUBLIC ".")
