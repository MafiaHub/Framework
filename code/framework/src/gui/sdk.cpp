/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

namespace Framework::GUI {
    GUIError SDK::Init(CefRefPtr<CefBrowser> browser) {
        _browser = browser;
        return GUIError::GUI_NONE;
    }

    void SDK::Shutdown() {
        _browser = nullptr;
    }
} // namespace Framework::GUI
