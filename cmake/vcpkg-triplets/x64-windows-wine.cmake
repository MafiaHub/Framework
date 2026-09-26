set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
# The container supplies the MSVC/SDK environment. There is no registered
# Visual Studio installation for vcpkg to discover through vswhere.
set(VCPKG_LOAD_VCVARS_ENV OFF)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/windows-wine.cmake")
set(VCPKG_ENV_PASSTHROUGH INCLUDE LIB LIBPATH VCToolsInstallDir)
# vcpkg hashes the compiler separately. Adding a host tool such as Python
# to PATH must not invalidate every already-built dependency.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PATH)
