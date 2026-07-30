/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "include/cef_app.h"
#include "include/cef_dom.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"

#include <unordered_map>

namespace Framework::GUI::CEF {
    class CallEventHandler final: public CefV8Handler {
      private:
        CefRefPtr<CefBrowser> _browser;

      public:
        explicit CallEventHandler(CefRefPtr<CefBrowser> browser): _browser(browser) {}

        bool Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) override;

        IMPLEMENT_REFCOUNTING(CallEventHandler);
    };

    class RendererApp final: public CefApp, public CefRenderProcessHandler {
      private:
        // browser id -> form control focused; a renderer process can host several browsers
        std::unordered_map<int, bool> _inputFocus;

      public:
        CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
            return this;
        }

        void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
        void OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefDOMNode> node) override;
        void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override;

        IMPLEMENT_REFCOUNTING(RendererApp);
    };
} // namespace Framework::GUI::CEF
