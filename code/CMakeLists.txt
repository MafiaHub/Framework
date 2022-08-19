# Load the global headers one by one (a few ones have relative include path, so we are forced to do so)
include_directories(PUBLIC
        "../vendors/mongoose"
        "../vendors/nodejs/include"
        "../vendors/glm"
        "../vendors/minhook/include"
        "../vendors/firebase/include"
        "../vendors/cppfs/source/cppfs/include"
        "../vendors/json/include"
        "../vendors/slikenet/Source"
        "../vendors/ntdll"
        "../vendors/semver"
        "../vendors/imgui"
        "../vendors/lua/src"
        "../vendors/sdl2/include"
        "../vendors/spdlog/include"
        "../vendors/optick/src"
        "../vendors/steamworks/include"
        "../vendors/galaxy/Include"
        "../vendors/udis86"
        "../vendors/httplib"
        "../vendors/lua-5.2.1/src"
        "../vendors/lua-5.3.6/src"
        "../vendors/lua-5.4.4/src"
        "../vendors/discord/src"
        "../vendors/sentry/include"
        "../vendors/curl/include"
        "../vendors/v8pp"
        "../vendors/fmt/include"
        "../vendors/libsig"
        "../vendors/sfd"
        "../vendors/cxxopts"
        "../vendors/fu2"
        "../vendors/freetype/include"
        "../vendors/StackWalker/Main/StackWalker"
        "../vendors"
        )

# Load the framework
add_subdirectory(framework)
add_subdirectory(tests)

# Load the projects
file(GLOB projects "projects/*")
foreach (proj ${projects})
    get_filename_component(proj_name ${proj} NAME)

    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/projects/${proj_name}/IGNORE")
        message("Loading ${proj_name}")
        add_subdirectory("projects/${proj_name}")
    else ()
        message("Skipping ${proj_name}")
    endif ()
endforeach ()
