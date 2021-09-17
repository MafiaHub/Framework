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
        "../vendors/spdlog/include"
        "../vendors/optick/src"
        "../vendors/steamworks/include"
        "../vendors/httplib"
        "../vendors/cef"
        "../vendors/discord/src"
        "../vendors/sentry/include"
        "../vendors/curl/include"
        "../vendors"
        )

# Load the framework
add_subdirectory(framework)
add_subdirectory(tests)

# Load the projects
file(GLOB projects "projects/*")
foreach (proj ${projects})
    get_filename_component(proj_name ${proj} NAME)
    add_subdirectory("projects/${proj_name}")
endforeach ()
