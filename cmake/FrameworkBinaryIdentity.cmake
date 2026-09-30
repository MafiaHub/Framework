# Product identity for a shipped Windows binary: a VERSIONINFO resource, an optional icon, and,
# for a launcher, the identity in its application manifest. Without it a binary carries no
# publisher or product for Explorer, SmartScreen or an antivirus reputation system to key on.
#
#   fw_set_binary_identity(<target>
#       PRODUCT <name> VERSION <major[.minor[.patch[.build]]]> COMPANY <name>
#       [DESCRIPTION <text>] [COPYRIGHT <text>] [ICON <path to .ico>])
#
# DESCRIPTION defaults to PRODUCT and COPYRIGHT to "Copyright (C) <COMPANY>". A target that links
# FrameworkLoader also gets these in its manifest; one that never calls this keeps the generic
# MafiaHub.Framework.Launcher manifest.

set(FW_LAUNCHER_MANIFEST_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../code/framework/src/launcher/launcher.manifest.in")
set(FW_BINARY_IDENTITY_RC_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/templates/binary_identity.rc.in")

function(fw_set_binary_identity target)
    if(NOT WIN32)
        return()
    endif()

    cmake_parse_arguments(ID "" "PRODUCT;VERSION;COMPANY;DESCRIPTION;COPYRIGHT;ICON" "" ${ARGN})
    foreach(required PRODUCT VERSION COMPANY)
        if("${ID_${required}}" STREQUAL "")
            message(FATAL_ERROR "fw_set_binary_identity(${target}) requires ${required}")
        endif()
    endforeach()
    if(NOT ID_VERSION MATCHES "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?(\\.[0-9]+)?$")
        message(FATAL_ERROR "fw_set_binary_identity(${target}): VERSION '${ID_VERSION}' must be one to four dot-separated numbers")
    endif()
    if(NOT ID_DESCRIPTION)
        set(ID_DESCRIPTION "${ID_PRODUCT}")
    endif()
    if(NOT ID_COPYRIGHT)
        set(ID_COPYRIGHT "Copyright (C) ${ID_COMPANY}")
    endif()

    # Both the fixed-size version and the manifest take exactly four numbers
    string(REPLACE "." ";" parts "${ID_VERSION}")
    list(LENGTH parts count)
    while(count LESS 4)
        list(APPEND parts 0)
        list(LENGTH parts count)
    endwhile()
    list(JOIN parts "," FW_ID_VERSION_COMMA)
    list(JOIN parts "." FW_ID_VERSION_DOTTED)

    # RC string literals escape a quote by doubling it
    foreach(field PRODUCT COMPANY DESCRIPTION COPYRIGHT)
        string(REPLACE "\"" "\"\"" FW_ID_${field} "${ID_${field}}")
    endforeach()
    set(FW_ID_VERSION "${ID_VERSION}")
    set(FW_ID_INTERNAL_NAME "${target}")

    set(FW_ID_ICON_LINE "")
    if(ID_ICON)
        get_filename_component(icon "${ID_ICON}" ABSOLUTE)
        if(NOT EXISTS "${icon}")
            message(FATAL_ERROR "fw_set_binary_identity(${target}): ICON '${icon}' does not exist")
        endif()
        # The lowest-numbered icon group is the one Explorer shows for the file
        set(FW_ID_ICON_LINE "1 ICON \"${icon}\"")
    endif()

    # Two passes: @-variables now, then $<TARGET_FILE_NAME> once the generator knows it
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/fw_identity")
    configure_file("${FW_BINARY_IDENTITY_RC_TEMPLATE}" "${dir}/${target}.rc.in" @ONLY)
    file(GENERATE OUTPUT "${dir}/${target}.rc" INPUT "${dir}/${target}.rc.in" TARGET ${target})
    target_sources(${target} PRIVATE "${dir}/${target}.rc")

    # Read by FrameworkLoader's /MANIFESTINPUT, so only a launcher actually embeds it
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
        string(REGEX REPLACE "[^A-Za-z0-9]" "" company_id "${ID_COMPANY}")
        string(REGEX REPLACE "[^A-Za-z0-9]" "" product_id "${ID_PRODUCT}")
        string(REGEX REPLACE "[^A-Za-z0-9]" "" target_id "${target}")
        set(FW_MANIFEST_NAME "${company_id}.${product_id}.${target_id}")
        set(FW_MANIFEST_VERSION "${FW_ID_VERSION_DOTTED}")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(FW_MANIFEST_ARCH "amd64")
        else()
            set(FW_MANIFEST_ARCH "x86")
        endif()
        string(REPLACE "&" "&amp;" FW_MANIFEST_DESCRIPTION "${ID_DESCRIPTION}")
        string(REPLACE "<" "&lt;" FW_MANIFEST_DESCRIPTION "${FW_MANIFEST_DESCRIPTION}")
        string(REPLACE ">" "&gt;" FW_MANIFEST_DESCRIPTION "${FW_MANIFEST_DESCRIPTION}")
        configure_file("${FW_LAUNCHER_MANIFEST_TEMPLATE}" "${dir}/${target}.manifest" @ONLY)
        set_target_properties(${target} PROPERTIES FW_LAUNCHER_MANIFEST "${dir}/${target}.manifest")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${dir}/${target}.manifest")
    endif()
endfunction()
