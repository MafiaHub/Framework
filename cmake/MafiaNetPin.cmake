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
set(MAFIANET_PIN_VERSION "0.23.0") # A Shutdown() from inside receive dispatch no longer frees the packet or transfer receiver in use; snake_case header names (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "b81fcd58707273b25e6e6ac3ce927f3b51ff6d53742bc042aac22e59a71fb953")
set(MAFIANET_PIN_SHA256_macos-arm64 "0723adfa047ffd8cf89ce5c0b1ddb1ffc088313bbbf8f25b95a321bbd0301a2b")
set(MAFIANET_PIN_SHA256_macos-x86_64 "cbb3ccc389a2a8444bad223db2bb0fe8d7c4818b48b3b40e56f744200c584ff5")
set(MAFIANET_PIN_SHA256_windows-x64 "baefa6694fdd2985940f307256b1410ccbd3a7dbd21f12ebc809c80b5d813e6c")
set(MAFIANET_PIN_SHA256_windows-x86 "75f70eafec2245994aa3130a89727f6241732831ce14f6b38dcd4edb33416c3e")
