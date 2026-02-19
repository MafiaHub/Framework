/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

namespace Framework::GUI::CEF {
    class App final: public CefApp, public CefBrowserProcessHandler {
      private:
        bool _contextInitialized = false;

      public:
        CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
            return this;
        }

        void OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) override;
        void OnContextInitialized() override;

        bool IsContextInitialized() const {
            return _contextInitialized;
        }

        IMPLEMENT_REFCOUNTING(App);
    };
} // namespace Framework::GUI::CEF
