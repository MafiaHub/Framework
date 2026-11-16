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

namespace Framework::GUI::CEF {
    // Carries the browser process id to every child, which exits when that process does
    inline constexpr const char *kParentProcessSwitch = "fw-parent-pid";

    class CallEventHandler final: public CefV8Handler {
      public:
        bool Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) override;

        IMPLEMENT_REFCOUNTING(CallEventHandler);
    };

    class RendererApp final
        : public CefApp
        , public CefRenderProcessHandler {
      public:
        CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
            return this;
        }

        void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
        void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
        void OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefDOMNode> node) override;

        IMPLEMENT_REFCOUNTING(RendererApp);
    };
} // namespace Framework::GUI::CEF
