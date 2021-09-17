# Find vcpkg on our system
find_file(
        VCPKG_SCRIPT
        vcpkg.cmake
        PATHS
        ${VCPKG_ROOT}
        ENV "VCPKG_ROOT"
        "~/Libraries/vcpkg"
        "~/vcpkg"
        "C:\\vcpkg"
        PATH_SUFFIXES
        "scripts/buildsystems"
        REQUIRED
)

# Effectively load the buildsystem
include(${VCPKG_SCRIPT})