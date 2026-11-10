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
set(MAFIANET_PIN_VERSION "0.25.1") # RakPeer only delivers a remote's library ids where a conforming peer sends them (patch, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "ebd9833762083b382302e8860e8d3fc1501b8ea1b010163030344f4a5e688505")
set(MAFIANET_PIN_SHA256_macos-arm64 "c384d68115090675d1232991963acb308763b3f2a9e3a104b080cb99315b4c17")
set(MAFIANET_PIN_SHA256_macos-x86_64 "25991952c6128b1c7371d3f31d2f1b70b2674d0178b6d9c185d46dd27435a792")
set(MAFIANET_PIN_SHA256_windows-x64 "9a6c54b112d82742b83fe17c0cc4d764e4dc7174942c4902698cc56deb6b70b5")
set(MAFIANET_PIN_SHA256_windows-x86 "f8a44c2430ae4326a224847109be76c11dc0fb8161160db46be85cf3f70b9662")
