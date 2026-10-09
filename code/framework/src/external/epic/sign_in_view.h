/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <functional>

namespace Framework::External::Epic {
    // Opens Epic's sign-in page in a CEF window, reads the authorization code off the redirect page
    // and persists the session. onDone(true) once signed in, false on failure or cancel. Call on the
    // CEF UI thread while the message loop runs.
    void ShowSignInWindow(std::function<void(bool)> onDone);
} // namespace Framework::External::Epic
