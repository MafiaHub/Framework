# Smoke test for the prebuilt Framework SDK: a project shaped like a mod, built against an install
# prefix alone (-DFW_SDK_ROOT) through the same FrameworkBootstrap.cmake a mod vendors. Driven by the
# FrameworkSdkSmoke target in code/tests/CMakeLists.txt; nothing here reads the Framework source tree
# except the bootstrap file under test.
cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FrameworkBootstrap.cmake")

project(FrameworkSdkConsumer CXX)

fw_bootstrap_finish()

if(Framework_HAS_SERVER OR TARGET Framework::Server)
    add_executable(SdkConsumerServer server.cpp)
    target_link_libraries(SdkConsumerServer PRIVATE Framework::Server)
    if(WIN32)
        target_compile_definitions(SdkConsumerServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
    endif()
    fw_stage_runtime(SdkConsumerServer SERVER)
endif()

if(WIN32)
    add_library(SdkConsumerClient SHARED client.cpp)
    target_link_libraries(SdkConsumerClient PRIVATE Framework::Client)
    target_compile_definitions(SdkConsumerClient PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_link_options(SdkConsumerClient PRIVATE /SAFESEH:NO /MANIFEST:NO)
    fw_stage_runtime(SdkConsumerClient CLIENT)

    add_executable(SdkConsumerLauncher launcher.cpp)
    target_link_libraries(SdkConsumerLauncher PRIVATE Framework::Loader SdkConsumerClient)
    target_compile_definitions(SdkConsumerLauncher PRIVATE NOMINMAX _USE_MATH_DEFINES)
    fw_set_binary_identity(SdkConsumerLauncher PRODUCT "Framework SDK Consumer" VERSION 0.0.1 COMPANY "MafiaHub" DESCRIPTION "SDK smoke test launcher")

    # The scripts a mod runs from its build (pattern table check) must be where the package says.
    if(NOT EXISTS "${Framework_SCRIPTS_DIR}/check_pattern_table.py")
        message(FATAL_ERROR "Framework_SCRIPTS_DIR (${Framework_SCRIPTS_DIR}) has no check_pattern_table.py")
    endif()
endif()
