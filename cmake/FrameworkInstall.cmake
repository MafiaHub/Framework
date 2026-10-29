# Install and export rules that turn a Framework build tree into the prebuilt SDK a project links
# against. `cmake --install <build> --prefix <dir>` lays out:
#
#   include/<name>/            headers, shared by every architecture: framework/, libnode/ (x64) or
#                              v8/ (x86), v8pp/, cef/, the vendors
#   lib/<arch>/                every static library and import library of one architecture, plus
#                              lib/<arch>/cmake/Framework (this package) and lib/<arch>/cmake/Tracy
#   lib/cmake/Framework/       the entry point find_package lands on; picks lib/<arch> by pointer size
#   runtime/<arch>/client/     files a client or launcher needs next to it (CEF, libnode.dll, helpers)
#   runtime/<arch>/server/     files a server needs next to it (libnode, crashpad_handler)
#   mafianet/<arch>/           the pinned MafiaNet archive, consumed through its own CMake package
#   share/framework/           scripts, vcpkg manifest and triplets, game_reserve.cpp
#   share/framework/<arch>/    launcher manifest, libnode_node_api.def, MANIFEST.json
#
# Architectures are disjoint subtrees, so the x86 and x64 install trees of one version overlay into a
# single archive; only the shared files are written twice, with identical content.
#
# Targets travel through install(EXPORT) as Framework::<name>. Imported targets cannot, so the vendor
# that creates one describes it with fw_sdk_imported_snippet() and FrameworkConfig.cmake re-creates it
# relative to the install prefix.

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FW_SDK_ARCH "x64")
    else()
        set(FW_SDK_ARCH "x86")
    endif()
else()
    set(FW_SDK_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(FW_SDK_INCLUDE_DIR "include")
set(FW_SDK_LIB_DIR "lib/${FW_SDK_ARCH}")
set(FW_SDK_RUNTIME_DIR "runtime/${FW_SDK_ARCH}")
set(FW_SDK_SHARE_DIR "share/framework")
set(FW_SDK_SHARE_ARCH_DIR "share/framework/${FW_SDK_ARCH}")
set(FW_SDK_CMAKE_DIR "${FW_SDK_LIB_DIR}/cmake/Framework")
set(FW_SDK_ENTRY_CMAKE_DIR "lib/cmake/Framework")
# Vendors with their own install rules (Tracy) read GNUInstallDirs; keep their libraries and
# packages inside this architecture's subtree.
set(CMAKE_INSTALL_LIBDIR "${FW_SDK_LIB_DIR}" CACHE PATH "" FORCE)

# Replaces a target's interface include directories with the build-tree directories given and the
# installed location, so install(EXPORT) can write them.
function(fw_sdk_export_includes target install_subdir)
    set(_dirs "")
    foreach(_dir IN LISTS ARGN)
        list(APPEND _dirs "$<BUILD_INTERFACE:${_dir}>")
    endforeach()
    set_property(TARGET ${target} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${_dirs} "$<INSTALL_INTERFACE:${FW_SDK_INCLUDE_DIR}/${install_subdir}>")
endfunction()

# Same for link directories; vendored import libraries are installed flat into lib/.
function(fw_sdk_export_link_directories target)
    set(_dirs "")
    foreach(_dir IN LISTS ARGN)
        list(APPEND _dirs "$<BUILD_INTERFACE:${_dir}>")
    endforeach()
    set_property(TARGET ${target} PROPERTY INTERFACE_LINK_DIRECTORIES ${_dirs} "$<INSTALL_INTERFACE:${FW_SDK_LIB_DIR}>")
endfunction()

# Copies the headers under each directory given into include/<install_subdir>, layout preserved.
function(fw_sdk_install_headers install_subdir)
    foreach(_dir IN LISTS ARGN)
        install(DIRECTORY "${_dir}/" DESTINATION "${FW_SDK_INCLUDE_DIR}/${install_subdir}"
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.hh" PATTERN "*.inl" PATTERN "*.ipp" PATTERN "*.inc" PATTERN "*.def")
    endforeach()
endfunction()

# Adds targets to the SDK export set. Shared libraries and executables land in runtime/client.
function(fw_sdk_install_targets)
    install(TARGETS ${ARGN} EXPORT FrameworkTargets
        ARCHIVE DESTINATION "${FW_SDK_LIB_DIR}"
        LIBRARY DESTINATION "${FW_SDK_LIB_DIR}"
        RUNTIME DESTINATION "${FW_SDK_RUNTIME_DIR}/client")
    set_property(GLOBAL APPEND PROPERTY FW_SDK_EXPORTED_TARGETS ${ARGN})
endfunction()

# Files a client (CLIENT), a server (SERVER) or both (COMMON) need next to the binary.
function(fw_sdk_runtime role)
    if(role STREQUAL "COMMON")
        set(_roles client server)
    else()
        string(TOLOWER "${role}" _roles)
    endif()
    foreach(_role IN LISTS _roles)
        foreach(_file IN LISTS ARGN)
            if(IS_DIRECTORY "${_file}")
                install(DIRECTORY "${_file}" DESTINATION "${FW_SDK_RUNTIME_DIR}/${_role}")
            else()
                install(PROGRAMS "${_file}" DESTINATION "${FW_SDK_RUNTIME_DIR}/${_role}")
            endif()
        endforeach()
    endforeach()
endfunction()

# CMake lines FrameworkConfig.cmake runs before the exported targets load; `\${PACKAGE_PREFIX_DIR}`
# (escaped) is the install prefix at that point.
function(fw_sdk_imported_snippet)
    # Per argument, not ${ARGN}: a quoted list argument (an INTERFACE_LINK_LIBRARIES value) must stay
    # one line.
    set(_text "")
    math(EXPR _last "${ARGC} - 1")
    foreach(_index RANGE 0 ${_last})
        if(NOT "${ARGV${_index}}" STREQUAL "")
            string(APPEND _text "${ARGV${_index}}\n")
        endif()
    endforeach()
    set_property(GLOBAL APPEND_STRING PROPERTY FW_SDK_IMPORTED_SNIPPET "${_text}")
endfunction()

# One fact about the archive, recorded in MANIFEST.json and FrameworkManifest.cmake (FW_SDK_<key>).
function(fw_sdk_manifest key value)
    set_property(GLOBAL APPEND PROPERTY FW_SDK_MANIFEST_KEYS "${key}")
    set_property(GLOBAL PROPERTY "FW_SDK_MANIFEST_${key}" "${value}")
endfunction()

# Writes the package files and the install rules that depend on everything the tree declared.
# Called once, at the end of the root CMakeLists.
function(fw_sdk_finalize)
    # Building the services adapter from source only produces the MafiaHub Services archive (see
    # .github/workflows/update_services.yml). Its target is private and outside the export set, so
    # install(EXPORT) would reject FrameworkServer's link to it; such a tree ships no SDK.
    if(MAFIAHUB_SERVICES_BUILD_FROM_SOURCE)
        message(STATUS "MafiaHub Services built from source: skipping the SDK package")
        return()
    endif()

    set(_cmake_dir "${Framework_SOURCE_DIR}/cmake")
    set(_gen_dir "${CMAKE_BINARY_DIR}/sdk-package")
    file(MAKE_DIRECTORY "${_gen_dir}")

    file(STRINGS "${Framework_SOURCE_DIR}/VERSION" _fw_version)
    string(STRIP "${_fw_version}" _fw_version)
    include(GetGitRevisionDescription)
    get_git_head_revision(_fw_refspec _fw_commit)

    if(WIN32)
        set(_platform "windows-${FW_SDK_ARCH}")
    elseif(APPLE)
        set(_platform "macos-${FW_SDK_ARCH}")
    else()
        set(_platform "linux-${FW_SDK_ARCH}")
    endif()

    fw_sdk_manifest(VERSION "${_fw_version}")
    fw_sdk_manifest(ARCH "${FW_SDK_ARCH}")
    fw_sdk_manifest(COMMIT "${_fw_commit}")
    fw_sdk_manifest(PLATFORM "${_platform}")
    fw_sdk_manifest(POINTER_SIZE "${CMAKE_SIZEOF_VOID_P}")
    fw_sdk_manifest(COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
    fw_sdk_manifest(COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
    if(MSVC)
        fw_sdk_manifest(MSVC_VERSION "${MSVC_VERSION}")
        fw_sdk_manifest(MSVC_TOOLSET_VERSION "${MSVC_TOOLSET_VERSION}")
    endif()
    fw_sdk_manifest(CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    fw_sdk_manifest(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    fw_sdk_manifest(VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
    fw_sdk_manifest(VCPKG_COMMIT "${FW_VCPKG_PIN}")
    fw_sdk_manifest(PROFILING "${FW_PROFILING}")
    fw_sdk_manifest(DLL_INJECTION "${FW_DLL_INJECTION}")
    fw_sdk_manifest(NODE_INSPECTOR "${FW_NODE_INSPECTOR}")
    if(TARGET FrameworkServer)
        fw_sdk_manifest(HAS_SERVER "ON")
    else()
        fw_sdk_manifest(HAS_SERVER "OFF")
    endif()
    if(TARGET FrameworkClient)
        fw_sdk_manifest(HAS_CLIENT "ON")
    else()
        fw_sdk_manifest(HAS_CLIENT "OFF")
    endif()

    get_property(_targets GLOBAL PROPERTY FW_SDK_EXPORTED_TARGETS)
    set(_aliases "")
    foreach(_target IN LISTS _targets)
        get_target_property(_export_name ${_target} EXPORT_NAME)
        if(NOT _export_name)
            set(_export_name "${_target}")
        endif()
        list(APPEND _aliases "${_target}=Framework::${_export_name}")
    endforeach()

    # FrameworkManifest.cmake + MANIFEST.json
    get_property(_keys GLOBAL PROPERTY FW_SDK_MANIFEST_KEYS)
    list(REMOVE_DUPLICATES _keys)
    set(_manifest_cmake "# Facts about the archive this package came from. Generated; read by FrameworkConfig.cmake.\n")
    set(_manifest_json "{\n")
    set(_first TRUE)
    foreach(_key IN LISTS _keys)
        get_property(_value GLOBAL PROPERTY "FW_SDK_MANIFEST_${_key}")
        string(APPEND _manifest_cmake "set(FW_SDK_${_key} \"${_value}\")\n")
        if(NOT _first)
            string(APPEND _manifest_json ",\n")
        endif()
        set(_first FALSE)
        string(REPLACE "\\" "\\\\" _json_value "${_value}")
        string(REPLACE "\"" "\\\"" _json_value "${_json_value}")
        string(TOLOWER "${_key}" _json_key)
        string(APPEND _manifest_json "  \"${_json_key}\": \"${_json_value}\"")
    endforeach()
    string(APPEND _manifest_json "\n}\n")
    string(APPEND _manifest_cmake "set(FW_SDK_TARGET_ALIASES \"${_aliases}\")\n")
    file(WRITE "${_gen_dir}/FrameworkManifest.cmake" "${_manifest_cmake}")
    file(WRITE "${_gen_dir}/MANIFEST.json" "${_manifest_json}")

    # FrameworkImported.cmake
    get_property(_snippet GLOBAL PROPERTY FW_SDK_IMPORTED_SNIPPET)
    file(WRITE "${_gen_dir}/FrameworkImported.cmake" "# Imported targets the exported Framework targets link. Generated; read by FrameworkConfig.cmake.\n\n${_snippet}")

    include(CMakePackageConfigHelpers)
    include(MafiaNetPin)
    set(MAFIANET_PIN_VERSION_FOR_CONFIG "${MAFIANET_PIN_VERSION}")
    configure_package_config_file("${_cmake_dir}/FrameworkConfig.cmake.in" "${_gen_dir}/FrameworkConfig.cmake"
        INSTALL_DESTINATION "${FW_SDK_CMAKE_DIR}"
        NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO)
    # ARCH_INDEPENDENT: the entry point serves every architecture in the prefix; the per-architecture
    # Config does the pointer-size check (FrameworkAbi.cmake).
    write_basic_package_version_file("${_gen_dir}/FrameworkConfigVersion.cmake" VERSION "${_fw_version}" COMPATIBILITY ExactVersion ARCH_INDEPENDENT)

    # The entry point: one file per prefix, the same for every architecture in it, dispatching on
    # the consumer's pointer size to the package under lib/<arch>.
    file(WRITE "${_gen_dir}/entry/FrameworkConfig.cmake" "# MafiaHub Framework SDK entry point. Generated; selects the architecture's package.
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_fw_sdk_arch \"x64\")
    else()
        set(_fw_sdk_arch \"x86\")
    endif()
else()
    set(_fw_sdk_arch \"\${CMAKE_SYSTEM_PROCESSOR}\")
endif()
set(_fw_sdk_arch_config \"\${CMAKE_CURRENT_LIST_DIR}/../../\${_fw_sdk_arch}/cmake/Framework/FrameworkConfig.cmake\")
if(NOT EXISTS \"\${_fw_sdk_arch_config}\")
    get_filename_component(_fw_sdk_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)
    message(FATAL_ERROR \"The Framework SDK at \${_fw_sdk_prefix} has no \${_fw_sdk_arch} libraries (lib/\${_fw_sdk_arch} is missing)\")
endif()
include(\"\${_fw_sdk_arch_config}\")
")

    install(EXPORT FrameworkTargets NAMESPACE Framework:: DESTINATION "${FW_SDK_CMAKE_DIR}" FILE FrameworkTargets.cmake)
    # Architecture-independent modules sit with the entry point, where a project's module path
    # finds them before it knows its pointer size.
    install(FILES
        "${_gen_dir}/entry/FrameworkConfig.cmake"
        "${_gen_dir}/FrameworkConfigVersion.cmake"
        "${_cmake_dir}/FrameworkAbi.cmake"
        "${_cmake_dir}/FrameworkRuntime.cmake"
        "${_cmake_dir}/FrameworkSetup.cmake"
        "${_cmake_dir}/FrameworkBinaryIdentity.cmake"
        "${_cmake_dir}/UpdateGitHash.cmake"
        "${_cmake_dir}/GetGitRevisionDescription.cmake"
        "${_cmake_dir}/GetGitRevisionDescription.cmake.in"
        "${_cmake_dir}/MSVCUtf8.cmake"
        DESTINATION "${FW_SDK_ENTRY_CMAKE_DIR}")
    install(FILES
        "${_cmake_dir}/templates/binary_identity.rc.in"
        "${Framework_SOURCE_DIR}/code/framework/src/launcher/launcher.manifest.in"
        DESTINATION "${FW_SDK_ENTRY_CMAKE_DIR}/templates")
    install(FILES
        "${_gen_dir}/FrameworkConfig.cmake"
        "${_gen_dir}/FrameworkConfigVersion.cmake"
        "${_gen_dir}/FrameworkManifest.cmake"
        "${_gen_dir}/FrameworkImported.cmake"
        DESTINATION "${FW_SDK_CMAKE_DIR}")
    install(FILES "${_gen_dir}/MANIFEST.json" DESTINATION "${FW_SDK_SHARE_ARCH_DIR}")
    install(FILES
        "${Framework_SOURCE_DIR}/vcpkg.json"
        "${_cmake_dir}/VcpkgBootstrap.cmake"
        "${_cmake_dir}/FrameworkBootstrap.cmake"
        "${Framework_SOURCE_DIR}/LICENSE.txt"
        DESTINATION "${FW_SDK_SHARE_DIR}")
    install(DIRECTORY "${_cmake_dir}/vcpkg-triplets" DESTINATION "${FW_SDK_SHARE_DIR}")
    install(PROGRAMS
        "${Framework_SOURCE_DIR}/scripts/check_pattern_table.py"
        "${Framework_SOURCE_DIR}/scripts/build_pattern_table.py"
        "${Framework_SOURCE_DIR}/scripts/ninja_utf8.cmd"
        DESTINATION "${FW_SDK_SHARE_DIR}/scripts")
    # The scripting contract and types-package tooling projects wrap (scripts/build_scripting_contract.mjs
    # and friends in each project).
    install(DIRECTORY "${Framework_SOURCE_DIR}/scripts/scripting-api" DESTINATION "${FW_SDK_SHARE_DIR}/scripts")
    install(FILES "${Framework_SOURCE_DIR}/code/tests/unit.h" DESTINATION "${FW_SDK_INCLUDE_DIR}/framework-tests")
endfunction()
