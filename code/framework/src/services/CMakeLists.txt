if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt")
    message(STATUS "MafiaHub Services source module is present, loading the project...")
    add_subdirectory(src)
else()
    message(STATUS "MafiaHub Services source module is not present, checking for a new version...")
    
    # Define release URLs
    set(MAFIA_HUB_RELEASE_URL "https://github.com/MafiaHub/Framework/releases/tag/MafiaHub-Services")
    set(MAFIA_HUB_DOWNLOAD_URL "https://github.com/MafiaHub/Framework/releases/download/MafiaHub-Services")
    
    # Create cache directory if it doesn't exist
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cache")
    
    # Define local build.txt path
    set(LOCAL_BUILD_TXT "${CMAKE_CURRENT_SOURCE_DIR}/cache/build.txt")
    
    # Try to download remote build.txt
    file(DOWNLOAD
        "${MAFIA_HUB_DOWNLOAD_URL}/build.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/cache/remote_build.txt"
        STATUS DOWNLOAD_STATUS
        SHOW_PROGRESS
        TLS_VERIFY ON
    )
    
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    
    # Check if download was successful
    if(${STATUS_CODE} EQUAL 0)
        # Read the remote build timestamp
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cache/remote_build.txt" REMOTE_BUILD_TIMESTAMP)
        string(STRIP "${REMOTE_BUILD_TIMESTAMP}" REMOTE_BUILD_TIMESTAMP)
        
        set(NEED_UPDATE TRUE)
        
        # Check if local build.txt exists
        if(EXISTS "${LOCAL_BUILD_TXT}")
            # Read the local build timestamp
            file(READ "${LOCAL_BUILD_TXT}" LOCAL_BUILD_TIMESTAMP)
            string(STRIP "${LOCAL_BUILD_TIMESTAMP}" LOCAL_BUILD_TIMESTAMP)
            
            # Compare timestamps
            if("${LOCAL_BUILD_TIMESTAMP}" STREQUAL "${REMOTE_BUILD_TIMESTAMP}")
                message(STATUS "MafiaHub Services is up to date (build: ${LOCAL_BUILD_TIMESTAMP})")
                set(NEED_UPDATE FALSE)
            else()
                message(STATUS "MafiaHub Services needs to be updated (local: ${LOCAL_BUILD_TIMESTAMP}, remote: ${REMOTE_BUILD_TIMESTAMP})")
            endif()
        else()
            message(STATUS "No local build.txt found, will download MafiaHub Services")
        endif()
        
        # Determine platform-specific zip file
        if(WIN32)
            set(PLATFORM_ZIP "mafiahub-services-windows-latest.zip")
            set(MAFIA_HUB_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/MafiaHubServices.lib")
        elseif(APPLE)
            set(PLATFORM_ZIP "mafiahub-services-macos-latest.zip")
            set(MAFIA_HUB_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/libMafiaHubServices_macos.a")
        elseif(UNIX)
            set(PLATFORM_ZIP "mafiahub-services-ubuntu-latest.zip")
            set(MAFIA_HUB_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/libMafiaHubServices.a")
        endif()
        
        # If update is needed, download and extract the zip file
        if(NEED_UPDATE)
            message(STATUS "Downloading MafiaHub Services: ${PLATFORM_ZIP}")
            
            # Download platform-specific zip
            file(DOWNLOAD
                "${MAFIA_HUB_DOWNLOAD_URL}/${PLATFORM_ZIP}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cache/${PLATFORM_ZIP}"
                STATUS ZIP_DOWNLOAD_STATUS
                SHOW_PROGRESS
                TLS_VERIFY ON
            )
            
            list(GET ZIP_DOWNLOAD_STATUS 0 ZIP_STATUS_CODE)
            
            if(${ZIP_STATUS_CODE} EQUAL 0)
                # Clear existing files if they exist (except cache)
                if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib")
                    file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
                endif()
                if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/services")
                    file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/services")
                endif()
                
                # Extract the zip file
                message(STATUS "Extracting MafiaHub Services to ${CMAKE_CURRENT_SOURCE_DIR}")
                
                # Use CMake's built-in extraction capability
                execute_process(
                    COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_CURRENT_SOURCE_DIR}/cache/${PLATFORM_ZIP}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    RESULT_VARIABLE EXTRACT_RESULT
                )
                
                if(NOT ${EXTRACT_RESULT} EQUAL 0)
                    message(FATAL_ERROR "Failed to extract MafiaHub Services")
                endif()
                
                # Update local build.txt
                file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/cache/remote_build.txt" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/cache")
                file(RENAME "${CMAKE_CURRENT_SOURCE_DIR}/cache/remote_build.txt" "${LOCAL_BUILD_TXT}")
                
                message(STATUS "MafiaHub Services successfully updated to build ${REMOTE_BUILD_TIMESTAMP}")
            else()
                message(FATAL_ERROR "Failed to download MafiaHub Services zip file")
            endif()
        endif()
    else()
        message(WARNING "Failed to check for MafiaHub Services updates, using local version if available")
    endif()
    
    # Check if library exists
    if(NOT EXISTS "${MAFIA_HUB_LIB}")
        message(FATAL_ERROR "MafiaHub Services prebuilt library not found at: ${MAFIA_HUB_LIB}")
    endif()
    
    # IMPORTED (not INTERFACE) so the prebuilt archive is a real link target: order-dependent linkers
    # can place it correctly, and a link group can capture the archive (an INTERFACE wrapper hides it).
    add_library(MafiaHubServices STATIC IMPORTED GLOBAL)
    set_target_properties(MafiaHubServices PROPERTIES
        IMPORTED_LOCATION "${MAFIA_HUB_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/services")
endif()
