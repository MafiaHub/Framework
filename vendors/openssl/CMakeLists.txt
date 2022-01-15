cmake_minimum_required(VERSION 3.7)
project(build-openssl
        VERSION 1.0
        LANGUAGES C)
include(ExternalProject)

OPTION(BUILD_DIR "where is the working directory for the build" "${CMAKE_BINARY_DIR}/build-openssl")
OPTION(INSTALL_DIR "where to install built OpenSSL package" "${CMAKE_BINARY_DIR}/install-openssl")
MESSAGE(STATUS "BUILD-OPENSSL OpenSSL will be installed to ${INSTALL_DIR}, working DIR ${BUILD_DIR}")

ExternalProject_Add(openssl-cmake
        PREFIX ${BUILD_DIR}
        URL "https://github.com/MafiaHub/openssl-cmake/archive/1.1.1g-20200510.zip"
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} -DWITH_APPS=OFF
        )
