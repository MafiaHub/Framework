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
set(MAFIANET_PIN_VERSION "0.19.0") # RakVoice receive ordering + in-band FEC, unique Linux GUIDs (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "dfad8bccfe66ee968b3b242b742988a79cbcd95c5b6b2b5c83ceaa9cbed1a7ad")
set(MAFIANET_PIN_SHA256_macos-arm64 "bf3458265fc17c2bf877457e357b0d707cc6a1e2272d0797fec684c3a6be99c6")
set(MAFIANET_PIN_SHA256_macos-x86_64 "3548b7489bc14bd83e9d16525e75578e4375a5b4fa73aa0f31c465d210e47703")
set(MAFIANET_PIN_SHA256_windows-x64 "4aafe6bc3f27ddbc7be7d9bde3b96cf36783c38b6f8ee0a58ba0caa9112eb5fd")
set(MAFIANET_PIN_SHA256_windows-x86 "a394d6a959cd4e69b8d613b6937666108c4ca25d5e6d8dceaa6a6b8502acfee4")
