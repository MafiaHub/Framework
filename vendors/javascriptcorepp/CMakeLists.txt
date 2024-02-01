cmake_minimum_required(VERSION 3.3.2)
project(JavaScriptCorePP VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(JAVASCRIPTCORE_INCLUDE "internal" CACHE STRING "Directory of JavaScriptCore includes")
set(INVALID_CAST_THROW True CACHE BOOL "Should JavaScriptCorePP throw exception on invalid js value casts")
set(JS_ERROR_THROW False CACHE BOOL "Should JavaScriptCorePP throw exception when a called js function returns error/exception")

ADD_LIBRARY( JavaScriptCorePP STATIC
             src/JSContext.cpp
             src/JSFunction.cpp
             src/JSHelper.cpp
             src/JSObject.cpp
             src/JSString.cpp
             src/JSValue.cpp
             src/JSPromise.cpp
           )

target_include_directories(JavaScriptCorePP PRIVATE ${JAVASCRIPTCORE_INCLUDE})
target_include_directories(JavaScriptCorePP PUBLIC "include")

IF(INVALID_CAST_THROW)
	add_compile_definitions("INVALID_CAST_THROW")
endif()

iF(JS_ERROR_THROW)
	add_compile_definitions("JS_ERROR_THROW")
endif()