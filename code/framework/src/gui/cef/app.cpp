/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "app.h"
#include "include/cef_parser.h"

namespace Framework::GUI::CEF {

    CefRefPtr<CefResourceHandler> App::Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &scheme_name, CefRefPtr<CefRequest> request) {
        if (!browser || !frame)
            return nullptr;

        CefURLParts urlParts;
        if (!CefParseURL(request->GetURL(), urlParts))
            return nullptr;

        std::string scheme = CefString(&urlParts.scheme).ToString();
        std::string domain = CefString(&urlParts.host).ToString();

        SchemaHandlerFactoryCallback handler;
        {
            std::scoped_lock lock(_handlersMutex);
            auto it = _handlers.find({scheme, domain});
            if (it == _handlers.end())
                return nullptr;
            handler = it->second;
        }

        // call outside the lock: the callback may be slow and must not race re-registration
        return handler(browser, frame, scheme_name, request);
    }

    void App::OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) {
        commandLine->AppendSwitch("disable-gpu-compositing");
        if (!_gpuAccelerated) {
            // CPU OSR path never touches the driver; a crashing GPU process otherwise
            // takes the whole browser down ("GPU process isn't usable")
            commandLine->AppendSwitch("disable-gpu");
        }
        commandLine->AppendSwitch("disable-extensions");
        commandLine->AppendSwitch("disable-pdf-extension");
        commandLine->AppendSwitch("disable-spell-checking");
        commandLine->AppendSwitch("disable-component-update");
        commandLine->AppendSwitchWithValue("disable-features", "WebUSB,WebHID");
        // Allow UI audio without a user gesture: CEF views are host-driven overlays (notifications,
        // HUD, menus) that never receive a real "user activation", so the default autoplay policy
        // would silently block all sound (Web Audio / <audio>). This opts the embedded browser out.
        commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        // No internal begin-frame scheduler: rendering uses external begin frames.
    }

    void App::OnContextInitialized() {
        _contextInitialized = true;
    }

    void App::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        // In --single-process mode the renderer runs inside the browser process,
        // so App (not RendererApp) receives this callback. Register the same
        // JS bindings that RendererApp::OnContextCreated registers in multi-process mode.
        CefRefPtr<CefV8Value> global  = context->GetGlobal();
        CefRefPtr<CefV8Handler> handler = new CallEventHandler(browser);
        CefRefPtr<CefV8Value> func    = CefV8Value::CreateFunction("callEvent", handler);
        global->SetValue("callEvent", func, V8_PROPERTY_ATTRIBUTE_NONE);
    }

    void App::RegisterSchemeHandlerFactory(const std::string &scheme, const std::string &domain, SchemaHandlerFactoryCallback callback) {
        std::scoped_lock lock(_handlersMutex);
        _handlers[{scheme, domain}] = std::move(callback);
    }
} // namespace Framework::GUI::CEF
