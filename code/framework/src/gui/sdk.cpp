/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

namespace Framework::GUI {
    bool SDK::Init(CefRefPtr<CefBrowser> browser) {
        _browser = browser;
        return true;
    }

    bool SDK::Shutdown() {
        _browser = nullptr;
        return true;
    }
} // namespace Framework::GUI
