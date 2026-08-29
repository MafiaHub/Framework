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
set(MAFIANET_PIN_VERSION "0.15.0") # session handshake (RAKNET_PROTOCOL_VERSION 7)

set(MAFIANET_PIN_SHA256_linux-x86_64 "860a230db94956847a02ca8022c0723bd73afb7db2b9be4c901d58d38c43b0d6")
set(MAFIANET_PIN_SHA256_macos-arm64 "7387fe0fa5801051f29e6b61b9eecb358cf8644d734ed30512513dce16680b64")
set(MAFIANET_PIN_SHA256_macos-x86_64 "db3500e51d3caa69c7beae43aea6c2a036e2fbe600dbd941c46127ab3bf2e774")
set(MAFIANET_PIN_SHA256_windows-x64 "fbef4cd7a5dd32f2ea6d0bc9b0ec41514c3750c696509ce431b9dbe5cfc75227")
set(MAFIANET_PIN_SHA256_windows-x86 "794bb615645524caa990cbbd1a864e97f2d38c3966d98c882b79e51bd4cdfa13")
