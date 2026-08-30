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
    // of its query and fragment and of its leading slash, with "index.html"
    // supplied for a directory-shaped request.
    //
    // Returns false for anything that could only be an attempt to leave the
    // root, or that is malformed. The decision is made here, on the URL, before
    // any provider maps it onto a filesystem, so a provider not backed by files
    // is covered by the same rule as one that is.
    //
    // Deliberately free of any CEF dependency: this is the security boundary of
    // the whole resource scheme, so it is written to be exercised directly by
    // the unit tests rather than only through a running browser.
    bool NormalizeResourcePath(const std::string &urlPath, std::string &normalized);
} // namespace Framework::GUI::Resources
