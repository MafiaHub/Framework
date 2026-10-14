# Writes a module-definition file that forwards Node's C API out of an executable into libnode.dll.
#
# Native npm addons (better-sqlite3, bcrypt, ...) are built by node-gyp against node.exe, and its
# delay-load hook resolves those imports from the host executable. A server embedding libnode is not
# node.exe: without these forwarders the first N-API call fails inside the delay-load helper and
# the process dies. Electron solves the same problem the same way.
#
# Run with `cmake -P`. Takes DUMPBIN, IMPLIB (libnode.lib), DLL_NAME (libnode, no extension) and
# OUTPUT. The names come from the import library itself, so they follow whatever libnode version is
# pinned without a list to keep in step.

execute_process(
    COMMAND "${DUMPBIN}" /nologo /exports "${IMPLIB}"
    OUTPUT_VARIABLE exports
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "node_api_exports: dumpbin failed on ${IMPLIB} (${result})")
endif()

# One plain C name per line: napi_*, node_api_* and libuv's uv_*, which addons may also call. Only the
# line end is matched: a match that also took the newline before it would leave the next line none,
# and every other export would be skipped.
string(REGEX MATCHALL "[ \t](napi_|node_api_|uv_)[A-Za-z0-9_]+[ \t]*\r?\n" lines "${exports}\n")
set(names "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" name)
    list(APPEND names "${name}")
endforeach()
list(REMOVE_DUPLICATES names)
list(SORT names)

list(LENGTH names count)
if(count EQUAL 0)
    message(FATAL_ERROR "node_api_exports: no N-API exports found in ${IMPLIB}")
endif()

set(content "EXPORTS\n")
foreach(name IN LISTS names)
    string(APPEND content "    ${name}=${DLL_NAME}.${name}\n")
endforeach()

# Only touch the file when the list changed, so the executable does not relink for nothing.
set(previous "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
endif()
if(NOT previous STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
endif()
message(STATUS "node_api_exports: ${count} forwarders to ${DLL_NAME}.dll")
