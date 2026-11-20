# vcpkg's own port, fetched by commit instead of by tag. Upstream moved the v1.8.3 tag on 2026-11-18
# (libunwind/libunwind#1076) from a commit on no branch to the head of v1.8-stable: the same tree, so
# the same source, but GitHub's generated tarball carries the commit and changed hash, and its cache
# served both for days (microsoft/vcpkg#55159). A commit cannot move; this one is on a branch, so it
# is not collected either. Same files as the v1.8.3 tarball.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO "libunwind/libunwind"
    REF 7bda80a564cddc5b215a84509026030cf16dfd94 # v1.8.3
    HEAD_REF master
    SHA512 e41b3b4bf559d4ed97863d21838b1d729afb1edd4d4e566a102bbc35555d5e4db3233aa593d4d6c393404851bdf631224aa72996586e0cbe581fcc7edcc5c5bd
    PATCHES
        liblzma.diff
)

vcpkg_find_acquire_program(PKGCONFIG)

vcpkg_make_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    AUTORECONF
    OPTIONS
        --disable-documentation
        --disable-tests
        --disable-zlibdebuginfo
        --enable-minidebuginfo
)
vcpkg_make_install()
vcpkg_fixup_pkgconfig()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
