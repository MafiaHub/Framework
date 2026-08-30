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
    // Turns a URL path into the path a provider sees: percent-decoded, stripped
    // of query, fragment and leading slash, with "index.html" supplied for a
    // directory-shaped request. False for anything malformed or that could only
    // be an attempt to leave the root.
    //
    // This is the security boundary of the scheme, and it decides on the URL
    // before any provider maps it onto a filesystem. Free of CEF so the tests
    // can drive it directly.
    bool NormalizeResourcePath(const std::string &urlPath, std::string &normalized);
} // namespace Framework::GUI::Resources
