# Load the framework
add_subdirectory(framework)
add_subdirectory(tests)

# Load the projects
if(FW_STANDALONE)
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
endif()
