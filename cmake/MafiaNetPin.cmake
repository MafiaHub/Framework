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
set(MAFIANET_PIN_VERSION "0.22.1") # FileListTransfer reference push window; DirectoryDeltaTransfer compares AddFile uploads by hash and writes streamed downloads as they arrive (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "fc554afe031a4c4b3b064d0beb96aa40c42b3b92337df87fcf0cbeb0f338626d")
set(MAFIANET_PIN_SHA256_macos-arm64 "a36e7ff318e117c34b84822ef54f9d8c3cdcf971945cb7ebe34582ca2636a35b")
set(MAFIANET_PIN_SHA256_macos-x86_64 "234cfca4c6bd4d88d2fa0a6f97dbd3ecadde6ac6eee93ced816fc9af06cef8a1")
set(MAFIANET_PIN_SHA256_windows-x64 "14c6317d6ddb2299de8fdff3c71373bb1c25fbab2b491187d28db4e8a7356e4e")
set(MAFIANET_PIN_SHA256_windows-x86 "b39568dabbf241d291a0f8e1ef2bde4f156edd6bad9f6cda282d4fed571e0c77")
