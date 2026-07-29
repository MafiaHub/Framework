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
# Raising MAFIANET_PIN across a MAJOR or MINOR MafiaNet version is a breaking
# change for the Framework too, and bump_version.sh treats a change to this file
# as a major bump on that basis. A PATCH-only bump is wire-compatible by
# MafiaNet's own versioning, but still lands here so the pin has a single home.
#
# Note it must NOT live under vendors/: .gitignore carries `vendors/**/*.cmake`,
# which would silently exclude it from the repository.

set(MAFIANET_PIN "v0.13.0" CACHE STRING "MafiaNet release tag to build against")
mark_as_advanced(MAFIANET_PIN)
