# MafiaNet dependency pin.
#
# Deliberately its own file rather than a line inside vendors/CMakeLists.txt.
# MafiaNet's message-id enum is positional: inserting an id shifts every id after
# it and breaks every peer built against the old header. Framework's release
# tooling classifies a version bump by which paths a change touches
# (.github/bump_version.sh), so the pin needs a path of its own that means
# exactly one thing -- "the wire format may have moved" -- instead of being
# buried among unrelated vendor edits.
#
# Raising this pin across a MAJOR or MINOR MafiaNet version is a breaking change
# for the Framework too, and bump_version.sh treats a change to this file as a
# major bump on that basis. A PATCH-only bump is wire-compatible by MafiaNet's own
# versioning, but still lands here so the pin has a single home.
#
# Note it must NOT live under vendors/: .gitignore carries `vendors/**/*.cmake`,
# which would silently exclude it from the repository.

# The pin names a MafiaNet release whose precompiled per-platform archives are
# downloaded at configure time (see vendors/CMakeLists.txt). A tag is a mutable
# ref, so the version alone would not pin anything; the SHA-256 of each archive
# is what actually fixes the content -- file(DOWNLOAD EXPECTED_HASH) fails the
# configure if an archive is ever repointed or tampered with. When bumping,
# update the version and all five hashes together (they are printed by the
# MafiaNet release pipeline, or: shasum -a 256 MafiaNet-<ver>-*).
#
# Plain set(), not CACHE entries: a cached value survives in an existing build
# tree, so bumping the pin here and reconfiguring incrementally would silently
# keep the old release. This file is the single source of truth, and there is no
# reason to let -D override the wire format of the protocol.
set(MAFIANET_PIN_VERSION "0.24.0") # Voice relays and runs its codec on the network thread; SetRelayHost takes the relay function (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "5b3241ae7b2fd6abb43b6c1fd0003d0006f8ff5dffad927684dadda2125a3d64")
set(MAFIANET_PIN_SHA256_macos-arm64 "6ba94b86d9deeaec90fc61101dfe3bf73d6cfc49f22dcda0ee19aafcd8e86dc1")
set(MAFIANET_PIN_SHA256_macos-x86_64 "7bc2a2effe9eadf63e5e44107c04c9676e1c3a9999fc03cb4db29b24f345540a")
set(MAFIANET_PIN_SHA256_windows-x64 "f9a2a1da5453241ef9c95443eb5b2ec033604cf41f002a980bf1e4c8dd14aa3c")
set(MAFIANET_PIN_SHA256_windows-x86 "546c6f037628d7e7507d5530c7d9776f841cdc4a3e9e68c59ef0f4789f2a8482")
