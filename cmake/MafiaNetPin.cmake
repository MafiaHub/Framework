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
set(MAFIANET_PIN_VERSION "0.20.0") # RM3 sends no empty channel groups, TwoWayAuthentication timeout, StatisticsHistory sample interval (RAKNET_PROTOCOL_VERSION still 7, wire-compatible)

set(MAFIANET_PIN_SHA256_linux-x86_64 "ad248dc8d4969937d251c01650410f255b148ba688b073ef6c161aae59442347")
set(MAFIANET_PIN_SHA256_macos-arm64 "3b5a96a8888a2187b5c5b266f49bd37aaac349e2885b4509d464d91e47be0206")
set(MAFIANET_PIN_SHA256_macos-x86_64 "c06b5c4296c3d7f93e1a71fb8e435101ae14e60e3f7ac72ada1ab67269e538d0")
set(MAFIANET_PIN_SHA256_windows-x64 "7bb0744de5e0dd4350799e92e1ca8b0426810f286e44ade941c4e870c4ccd692")
set(MAFIANET_PIN_SHA256_windows-x86 "f6387cf99152c7f81153c59870119fd7917fc58c85221e23bc0383dc04de94be")
