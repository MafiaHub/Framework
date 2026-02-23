include(FetchContent)

FetchContent_Declare(
    v8pp
    GIT_REPOSITORY https://github.com/pmed/v8pp.git
    GIT_TAG 04653b9  # Latest master with V8 14.x support (post v2.1.1)
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR nonexistent  # Prevent v8pp's own CMakeLists from being processed
)

FetchContent_MakeAvailable(v8pp)

message(STATUS "v8pp: Header-only V8 binding library (04653b9)")

# Configure v8pp config.hpp
set(V8PP_ISOLATE_DATA_SLOT 3 CACHE STRING "v8::Isolate data slot number (slot 3 to avoid conflict with Node.js)")
set(V8PP_PLUGIN_INIT_PROC_NAME v8pp_plugin_init CACHE STRING "v8pp plugin init procedure name")
if(WIN32)
    set(V8PP_PLUGIN_SUFFIX ".dll" CACHE STRING "v8pp plugin filename suffix")
elseif(APPLE)
    set(V8PP_PLUGIN_SUFFIX ".dylib" CACHE STRING "v8pp plugin filename suffix")
else()
    set(V8PP_PLUGIN_SUFFIX ".so" CACHE STRING "v8pp plugin filename suffix")
endif()
set(V8PP_HEADER_ONLY 1 CACHE STRING "Use v8pp as header-only library")
set(V8PP_USE_STD_STRING_VIEW 1 CACHE STRING "Use std::string_view")
set(PROJECT_VERSION "2.1.1")

configure_file(
    ${v8pp_SOURCE_DIR}/v8pp/config.hpp.in
    ${v8pp_SOURCE_DIR}/v8pp/config.hpp
)

# v8pp is header-only, create interface target
add_library(v8pp INTERFACE)
target_include_directories(v8pp INTERFACE ${v8pp_SOURCE_DIR})
target_link_libraries(v8pp INTERFACE v8)
