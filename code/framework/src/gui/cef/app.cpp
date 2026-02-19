/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "app.h"

namespace Framework::GUI::CEF {
    void App::OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) {
        commandLine->AppendSwitch("disable-gpu-compositing");
        commandLine->AppendSwitch("disable-extensions");
        commandLine->AppendSwitch("disable-pdf-extension");
        commandLine->AppendSwitch("disable-spell-checking");
        commandLine->AppendSwitch("disable-component-update");
        commandLine->AppendSwitch("enable-begin-frame-scheduling");
    }

    void App::OnContextInitialized() {
        _contextInitialized = true;
    }
} // namespace Framework::GUI::CEF
