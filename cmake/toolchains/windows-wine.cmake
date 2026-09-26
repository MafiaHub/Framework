# Windows CMake runs inside Wine with the SDK environment supplied by the
# container, so no Visual Studio installation discovery is necessary.
include("${CMAKE_CURRENT_LIST_DIR}/../../vendors/vcpkg/scripts/toolchains/windows.cmake")
set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
