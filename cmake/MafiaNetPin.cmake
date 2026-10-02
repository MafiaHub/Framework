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
set(MAFIANET_PIN_VERSION "0.21.1") # RakVoice sends no DTX comfort-noise refresh frames (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "4334fada9dc21305a5f0c96a9e46d907258a229a94ee98be6002eb57c192f92a")
set(MAFIANET_PIN_SHA256_macos-arm64 "7eecac8b58c96ec86597919528ab55bf5d62d75bfd65fda54230f6886c94fa78")
set(MAFIANET_PIN_SHA256_macos-x86_64 "cc15f42220d255b4f4b952cfc06eb302203b3ffa50a0dfe6bcc836f97584b246")
set(MAFIANET_PIN_SHA256_windows-x64 "c5ae1f5d4876a7d4d4ae6dcd02d9d77369750ce541443d3ced4bb1a086fe52c6")
set(MAFIANET_PIN_SHA256_windows-x86 "f319a632c53c2eb66bfe3763c88e246e360aed8ab986e0594c6f551c4dd713c9")
