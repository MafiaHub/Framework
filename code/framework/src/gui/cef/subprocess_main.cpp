/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <windows.h>

#include "include/cef_app.h"
#include "renderer_app.h"

int main(int argc, char *argv[]) {
    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<Framework::GUI::CEF::RendererApp> app(new Framework::GUI::CEF::RendererApp);
    return CefExecuteProcess(mainArgs, app, nullptr);
}
