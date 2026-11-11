# libnode / V8 dependency pin.
#
# The version names a MafiaHub/MafiaNode release whose prebuilt per-platform archives are
# downloaded at configure time (see vendors/libnode/CMakeLists.txt). A release asset is
# mutable, so the version alone pins nothing; the SHA-256 of each archive is what fixes the
# content -- file(DOWNLOAD EXPECTED_HASH) fails the configure if an archive is ever replaced,
# truncated or tampered with. When bumping, update the version and every hash together:
#   gh release download libnode-v<ver> --repo MafiaHub/MafiaNode --dir <scratch> && shasum -a 256 <scratch>/*
#
# Plain set(), not CACHE entries, for the reason given in MafiaNetPin.cmake: a cached value
# would survive in an existing build tree and silently keep the old release.
#
# It lives in cmake/ and not beside vendors/libnode/CMakeLists.txt because .gitignore carries
# `vendors/**/*.cmake`.
set(LIBNODE_PIN_VERSION "26.12.1")

# Full libnode (Node.js runtime), by platform suffix.
set(LIBNODE_PIN_SHA256_darwin-arm64 "9c24e47e0fb60ead974bc93b029ec80f2601b52db0526b0091a6def4cec52cd1")
set(LIBNODE_PIN_SHA256_linux-x64 "1771d3e1e4299cf5783fea3563ec4dbd9a05466068ad0e8ff622f8f02565a6eb")
set(LIBNODE_PIN_SHA256_win-x64 "b112e77ee8d7185742ad8510b1f2f727dbe3591642af6950e1443ebd44ac3063")

# Standalone V8 monolith (LIBNODE_V8_ONLY), by platform suffix.
set(V8_PIN_SHA256_win-x64 "e39266f279c2d577e647d4c3996bb1ec041e445eba6160e8d233d8ba679716ac")
set(V8_PIN_SHA256_win-x86 "9463adc49d600c15eac45a96b08ceadbb6b5b1294c57e384103ae234123ccee4")
