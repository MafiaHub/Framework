/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "app.h"

#include "include/cef_scheme.h"

#include <filesystem>
#include <string>

// CEF process bring-up and teardown, shared by the in-process Manager and the out-of-process host
// (gui/remote/host) so both run Chromium with the same settings, profile handling and diagnostics.
namespace Framework::GUI::CEF {
    // Guard the pump: some exit paths tear CEF down before our Shutdown runs.
    // TODO(cef-exit): an exit-path-independent Shutdown trigger would remove this.
    bool PumpMessageLoopGuarded();

    // CefInitialize with the Framework's settings, the fw:// factory and a ready global context.
    // |moduleDir| holds cef_subprocess.exe and the CEF runtime; |rootDir| gets logs/ and
    // cache/profiles/. On success the caller owns |profileLock| until CefShutdown has returned.
    bool StartRuntime(const std::string &rootDir, const std::filesystem::path &moduleDir, CefRefPtr<App> app, CefRefPtr<CefSchemeHandlerFactory> resourceFactory, HANDLE &profileLock);

    // Closes every browser the caller already asked to close, then CefShutdown. True when CEF
    // stopped; |pumpFailed| is set when the pump faulted on the way.
    bool StopRuntime(bool &pumpFailed);
} // namespace Framework::GUI::CEF
