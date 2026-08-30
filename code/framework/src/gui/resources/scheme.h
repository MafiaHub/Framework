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
    // The scheme the framework serves local content on. One fixed name rather
    // than a per-project one, because cef_subprocess.exe is a single shared
    // binary and every process in a CEF instance has to register the exact
    // same set of custom schemes. Projects claim hosts under it instead.
    inline constexpr const char *kResourceScheme = "fw";

    // Must be called from CefApp::OnRegisterCustomSchemes in *every* process. A
    // custom scheme registered only in the browser process is treated as opaque
    // in the renderer, which costs the page its origin and every API gated on
    // one.
    void RegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar);

    // Builds "fw://<host>/<path>" with exactly one separating slash.
    std::string MakeResourceURL(const std::string &host, const std::string &path);
} // namespace Framework::GUI::Resources
