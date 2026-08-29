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
set(MAFIANET_PIN_VERSION "0.18.0") # RakVoice::SetOrderingChannels (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "2b5aff6acb3cb8495cedafb94361c987836c61b40c748b860a4f07bf29dbd2c7")
set(MAFIANET_PIN_SHA256_macos-arm64 "08628084f65f6d635c9e2ca69301fd3465e50818c930b12ed6c52f931c2e725d")
set(MAFIANET_PIN_SHA256_macos-x86_64 "6489025e3045510d5c2ef37c494820570762582d0be24522740e4b745198ddbd")
set(MAFIANET_PIN_SHA256_windows-x64 "abe7533eacf9ae5f929d39e003f0e1c3638cb851cae7d799b6058bc037a02368")
set(MAFIANET_PIN_SHA256_windows-x86 "2766995cf725b2b682a9bf42c04e8d25ebcb273d653d690357210930d460b40a")
