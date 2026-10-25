# Load the framework
add_subdirectory(framework)

option(FW_BUILD_TESTS "Build the Framework test binaries" ${PROJECT_IS_TOP_LEVEL})
if(FW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Load the projects
if(FW_STANDALONE)
    file(GLOB projects "projects/*")
    foreach (proj ${projects})
        get_filename_component(proj_name ${proj} NAME)

        # Finder drops .DS_Store beside the projects; only directories are projects.
        if (NOT IS_DIRECTORY "${proj}")
            continue()
        endif ()

        if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/projects/${proj_name}/IGNORE")
            message("Loading ${proj_name}")
            # Not part of the Framework's ALL and, with it, of its install: `cmake --install` of this
            # tree produces the Framework SDK alone. Projects are always built by explicit target.
            add_subdirectory("projects/${proj_name}" EXCLUDE_FROM_ALL)
        else ()
            message("Skipping ${proj_name}")
        endif ()
    endforeach ()
endif()
