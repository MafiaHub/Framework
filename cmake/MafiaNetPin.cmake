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
set(MAFIANET_PIN_VERSION "0.21.0") # pending-session pool, session timeout, session status and abandonment for connection admission (RAKNET_PROTOCOL_VERSION still 7, new ids take reserved slots)

set(MAFIANET_PIN_SHA256_linux-x86_64 "6f95b8a1f61697e68b17b89fea8575ab3f140353fb98ebed86ac5af98f2cd27a")
set(MAFIANET_PIN_SHA256_macos-arm64 "2db384b4770c2a0186562011700dd58ab7c3c69cb206c1b5c4af576c09d48b96")
set(MAFIANET_PIN_SHA256_macos-x86_64 "918fc75dded6b8a519bfd6c59fa5f1ada5125fc2d2881f1b03c001eae6e20484")
set(MAFIANET_PIN_SHA256_windows-x64 "2bc716d7722455e7e70cab841bbca5d3f2cbf7a83ecfbe9386cb06d1f5700b8c")
set(MAFIANET_PIN_SHA256_windows-x86 "9e68925affc383afeb296defd01a2e311af10a7013df462581514ccd06fe3761")
