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
    // MIME type for a request path, from its extension. The table is ours
    // rather than Chromium's, which is built for downloads and misses .mjs,
    // .wasm and .webmanifest. Anything unlisted is application/octet-stream.
    std::string MimeTypeForPath(const std::string &path);

    // Types that must be tagged as UTF-8; without the tag Chromium guesses from
    // the locale and non-ASCII renders as mojibake.
    bool MimeTypeIsTextual(const std::string &mimeType);
} // namespace Framework::GUI::Resources
