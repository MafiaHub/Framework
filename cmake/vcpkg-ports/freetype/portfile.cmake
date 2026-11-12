# The upstream port as is; only the default features differ (vcpkg.json). Its patches resolve against CURRENT_PORT_DIR.
set(CURRENT_PORT_DIR "${VCPKG_ROOT_DIR}/ports/${PORT}")
include("${CURRENT_PORT_DIR}/portfile.cmake")
