/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_scheme.h"

#include <string>

namespace Framework::GUI::Resources {
    // One scheme for the whole framework: cef_subprocess.exe is a single shared
    // binary and every process must register an identical set of custom
    // schemes. Projects claim hosts under it.
    inline constexpr const char *kResourceScheme = "fw";

    // Call from CefApp::OnRegisterCustomSchemes in every process. A scheme
    // registered only in the browser process is opaque in the renderer, which
    // costs the page its origin.
    void RegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar);

    // "fw://<host>/<path>"
    std::string MakeResourceURL(const std::string &host, const std::string &path);
} // namespace Framework::GUI::Resources
