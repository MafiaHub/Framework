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

# Pinned by commit, not by tag. A tag is a mutable ref -- repointing it would
# change what every future build fetches while this file still reads the same --
# whereas a commit is what it is. Keep the human-readable release beside it.
#
# Plain set(), not a CACHE entry: a cached value survives in an existing build
# tree, so bumping the pin here and rebuilding incrementally would silently keep
# fetching the old revision. This file is the single source of truth, and there is
# no reason to let -D override the wire format of the protocol.
set(MAFIANET_PIN "2221f074cc32bf305c181d145fe4ba1550bab1b7") # v0.15.0 -- session handshake (RAKNET_PROTOCOL_VERSION 7)
