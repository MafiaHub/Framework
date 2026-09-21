set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Framework forces the /MD runtime in every configuration (cmake/FrameworkSetup.cmake) and already
# maps Debug onto the Release MafiaNet archive. Building the ports release-only keeps that story and
# halves a cold vcpkg install.
set(VCPKG_BUILD_TYPE release)
