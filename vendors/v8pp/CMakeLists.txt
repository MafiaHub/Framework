include(FetchContent)

FetchContent_Declare(
    v8pp
    GIT_REPOSITORY https://github.com/pmed/v8pp.git
    GIT_TAG v1.8.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # Prevent v8pp's own CMakeLists from being processed
)

FetchContent_MakeAvailable(v8pp)

message(STATUS "v8pp: Header-only V8 binding library v1.8.1")

# Configure v8pp config.hpp
set(V8PP_ISOLATE_DATA_SLOT 3 CACHE STRING "v8::Isolate data slot number (slot 3 to avoid conflict with Node.js)")
set(V8PP_PLUGIN_INIT_PROC_NAME v8pp_plugin_init CACHE STRING "v8pp plugin init procedure name")
set(V8PP_PLUGIN_SUFFIX ".dylib" CACHE STRING "v8pp plugin filename suffix")
set(V8PP_HEADER_ONLY 1 CACHE STRING "Use v8pp as header-only library")
set(V8PP_USE_STD_STRING_VIEW 1 CACHE STRING "Use std::string_view")
set(PROJECT_VERSION "1.8.1")

configure_file(
    ${v8pp_SOURCE_DIR}/v8pp/config.hpp.in
    ${v8pp_SOURCE_DIR}/v8pp/config.hpp
)

# Patch v8pp for V8 11.x+ compatibility (VisitHandlesWithClassIds was removed)
# Read the original function.hpp
file(READ ${v8pp_SOURCE_DIR}/v8pp/function.hpp V8PP_FUNCTION_HPP_CONTENT)

# Check if we need to patch (only if it contains the old code)
string(FIND "${V8PP_FUNCTION_HPP_CONTENT}" "VisitHandlesWithClassIds" V8PP_NEEDS_PATCH)
if(NOT V8PP_NEEDS_PATCH EQUAL -1)
    message(STATUS "v8pp: Patching function.hpp for V8 11.x+ compatibility")
    string(REPLACE
        "static void destroy_all(v8::Isolate* isolate)
	{
		handle_visitor visitor(isolate);
		isolate->VisitHandlesWithClassIds(&visitor);
	}"
        "static void destroy_all(v8::Isolate* isolate)
	{
		// Note: VisitHandlesWithClassIds was removed in V8 11.x+
		// The weak callbacks attached to value_holder objects will clean them up anyway.
		// This function is only called during explicit cleanup, not during normal operation.
		(void)isolate;
	}"
        V8PP_FUNCTION_HPP_CONTENT
        "${V8PP_FUNCTION_HPP_CONTENT}"
    )
    file(WRITE ${v8pp_SOURCE_DIR}/v8pp/function.hpp "${V8PP_FUNCTION_HPP_CONTENT}")
endif()

# v8pp is header-only, create interface target
add_library(v8pp INTERFACE)
target_include_directories(v8pp INTERFACE ${v8pp_SOURCE_DIR})
target_link_libraries(v8pp INTERFACE v8)
