# fw_add_cef_host()
#
# Adds FrameworkCefHost: the 64-bit remote CEF host (code/framework/src/gui/remote/host) and the
# 64-bit CEF runtime, staged together in ${CMAKE_BINARY_DIR}/bin/cef64 next to the client. The host
# is 64-bit whatever this tree targets, so it is a project of its own (code/framework/cef_host),
# configured and built inside this tree under cef_host64/ with a 64-bit compiler environment. It
# shares this tree's vcpkg checkout and CEF download directory, and always builds Release: it
# carries no game code worth debugging alongside the client.

include(ExternalProject)

function(fw_add_cef_host)
    if(TARGET FrameworkCefHost)
        return()
    endif()
    if(NOT MSVC)
        message(FATAL_ERROR "The remote CEF host builds with MSVC only")
    endif()

    # <VC>/Tools/MSVC/<version>/bin/Host<arch>/<arch>/cl.exe -> <VC>/Auxiliary/Build/vcvarsall.bat
    get_filename_component(_fw_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_fw_vc_root "${_fw_cl_dir}/../../../../../.." ABSOLUTE)
    set(_fw_vcvarsall "${_fw_vc_root}/Auxiliary/Build/vcvarsall.bat")
    if(NOT EXISTS "${_fw_vcvarsall}")
        message(FATAL_ERROR "FrameworkCefHost: no vcvarsall.bat at ${_fw_vcvarsall} (derived from ${CMAKE_CXX_COMPILER})")
    endif()
    file(TO_NATIVE_PATH "${_fw_vcvarsall}" _fw_vcvarsall_native)

    # The same toolset as this tree, not the newest installed: the client package ships one MSVC
    # runtime, taken from this compiler's redist, for both the game and the host.
    get_filename_component(_fw_toolset_dir "${_fw_cl_dir}/../../.." ABSOLUTE)
    get_filename_component(_fw_toolset_version "${_fw_toolset_dir}" NAME)
    if(NOT _fw_toolset_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$" OR NOT EXISTS "${_fw_toolset_dir}/bin/Hostx64/x64/cl.exe")
        message(FATAL_ERROR "FrameworkCefHost: no 64-bit compiler for MSVC toolset '${_fw_toolset_version}' under ${_fw_toolset_dir}")
    endif()

    set(_fw_host_binary "${CMAKE_BINARY_DIR}/cef_host64")
    set(_fw_host_output "${CMAKE_BINARY_DIR}/bin/cef64")

    # The outer build runs in this tree's compiler environment, possibly a 32-bit one. Its include
    # and library paths are dropped first so none of them reach the 64-bit link.
    set(_fw_env "${_fw_host_binary}/x64env.cmd")
    file(MAKE_DIRECTORY "${_fw_host_binary}")
    file(WRITE "${_fw_env}" "@echo off\r\nset INCLUDE=\r\nset LIB=\r\nset LIBPATH=\r\ncall \"${_fw_vcvarsall_native}\" amd64 -vcvars_ver=${_fw_toolset_version} >nul\r\nif errorlevel 1 exit /b 1\r\n%*\r\n")

    ExternalProject_Add(FrameworkCefHost
        SOURCE_DIR "${Framework_SOURCE_DIR}/code/framework/cef_host"
        BINARY_DIR "${_fw_host_binary}"
        # The compiler is named, not only put on PATH, so a toolset change reconfigures the host.
        CONFIGURE_COMMAND "${_fw_env}" "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR> -G Ninja
            "-DCMAKE_CXX_COMPILER=${_fw_toolset_dir}/bin/Hostx64/x64/cl.exe"
            "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -DCMAKE_BUILD_TYPE=Release
            "-DFW_VCPKG_ROOT=${FW_VCPKG_ROOT_RESOLVED}"
            "-DFW_CEF_DOWNLOAD_DIR=${FW_CEF_DOWNLOAD_DIR}"
            "-DCEF_VERSION=${CEF_VERSION}"
            "-DFW_VERSION=${FW_VERSION}"
            "-DFW_CEF_HOST_OUTPUT_DIR=${_fw_host_output}"
        BUILD_COMMAND "${_fw_env}" "${CMAKE_COMMAND}" --build <BINARY_DIR>
        INSTALL_COMMAND ""
        BUILD_ALWAYS OFF
        USES_TERMINAL_CONFIGURE ON
        USES_TERMINAL_BUILD ON
        BUILD_BYPRODUCTS "${_fw_host_output}/cef_host.exe"
    )

    # The build step reruns, and with it vcvarsall, only when something the host compiles changed.
    # The inner Ninja then decides what to rebuild; a missing cef_host.exe reruns it too.
    set(_fw_src "${Framework_SOURCE_DIR}/code/framework/src")
    file(GLOB_RECURSE _fw_host_inputs CONFIGURE_DEPENDS
        "${Framework_SOURCE_DIR}/code/framework/cef_host/*"
        "${_fw_src}/gui/remote/*"
        "${_fw_src}/gui/cef/*"
        "${_fw_src}/gui/resources/*"
        "${_fw_src}/logging/*"
        "${_fw_src}/utils/*.h")
    file(GLOB _fw_host_gui_inputs CONFIGURE_DEPENDS "${_fw_src}/gui/*.h" "${_fw_src}/gui/sdk.cpp")
    ExternalProject_Add_StepDependencies(FrameworkCefHost build
        ${_fw_host_inputs}
        ${_fw_host_gui_inputs}
        "${Framework_SOURCE_DIR}/cmake/DownloadCEF.cmake"
        "${Framework_SOURCE_DIR}/cmake/FrameworkBinaryIdentity.cmake"
        "${Framework_SOURCE_DIR}/cmake/VcpkgBootstrap.cmake")

    # SDK: next to the client runtime, where fw_stage_runtime copies it from.
    install(DIRECTORY "${_fw_host_output}/" DESTINATION "${FW_SDK_RUNTIME_DIR}/client/cef64" OPTIONAL)
endfunction()
