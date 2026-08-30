/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::GUI::Resources {
    // MIME type for a request path, chosen from its extension.
    //
    // The table is deliberately ours rather than Chromium's: Chromium's is
    // built for downloads, misses several things a modern page needs (.mjs,
    // .wasm, .webmanifest), and can shift between CEF releases. An extension
    // it does not name gets application/octet-stream, which with the nosniff
    // header the handler sends is the safe answer for a type nobody declared.
    std::string MimeTypeForPath(const std::string &path);

    // True for types whose bytes are text and must therefore be tagged as
    // UTF-8. Without the tag Chromium falls back to a locale guess and
    // non-ASCII content renders as mojibake.
    bool MimeTypeIsTextual(const std::string &mimeType);
} // namespace Framework::GUI::Resources
