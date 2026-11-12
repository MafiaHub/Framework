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
set(MAFIANET_PIN_VERSION "0.26.0") # RakPeer::WaitForPackets, and ReplicaManager3 receives on one thread while the pass runs on another (minor: new pure virtual, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "203859c3eb72bd8cd2960dc674fa7fcee74b1816ba4d0d91895b12b39bbe31d6")
set(MAFIANET_PIN_SHA256_macos-arm64 "6e253d1ff805a064b881e45880cbf0cc7e68099a2293597ad1caadf3dc6e2fd2")
set(MAFIANET_PIN_SHA256_macos-x86_64 "1a0a6a13c8e10c388cb954d7271b4f6c246afdd3bccc7bf4b4ac0b874629cf6b")
set(MAFIANET_PIN_SHA256_windows-x64 "d3bc96d7a3a3999e4119653fb0ba508de9d60ad3ae02bbc1c498be8b52e45bba")
set(MAFIANET_PIN_SHA256_windows-x86 "b17f21ce54c6dd65ead25bce627081bd43a19e2afdb45e45ef6f50adb79d883e")
