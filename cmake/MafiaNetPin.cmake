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
set(MAFIANET_PIN_VERSION "0.25.0") # ReplicaManager3 serializes broadcasts once per pass and can serve connections in parallel (Replica3/Connection_RM3 ABI change, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "3e95f9fdf93394649d5672b12037d7aaa2f6a84515940f3a9462cc5246d53e88")
set(MAFIANET_PIN_SHA256_macos-arm64 "c4a8ec4422724728ee43d70afb818c72b7a01450f14c6b9083747ac54d4569b3")
set(MAFIANET_PIN_SHA256_macos-x86_64 "5e5fba88dd5f8c7a5ee8cb462ed20688352620301cf074d40d60a21778d13ee8")
set(MAFIANET_PIN_SHA256_windows-x64 "e2b74bd11a9c97193173962ddaabd223e7c644cda20a1e285427970cdf2dfc1d")
set(MAFIANET_PIN_SHA256_windows-x86 "5beea4a205c3c02e3e23bd4db71993f900815c9e92d7f20a355d2003c0b5bfeb")
