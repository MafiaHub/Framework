/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "scheme.h"

namespace Framework::GUI::Resources {
    void RegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        // STANDARD gives the scheme a real origin, without which the origin lock
        // and relative URLs do not work. SECURE buys the secure-context APIs and
        // avoids mixed content. CORS allows a cross-host read and FETCH lets
        // fetch() reach the scheme at all.
        registrar->AddCustomScheme(kResourceScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    std::string MakeResourceURL(const std::string &host, const std::string &path) {
        std::string url = std::string(kResourceScheme) + "://" + host;
        if (path.empty() || path.front() != '/') {
            url += '/';
        }
        return url + path;
    }
} // namespace Framework::GUI::Resources
