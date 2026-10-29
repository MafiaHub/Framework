/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "app.h"

#include "gui/resources/scheme.h"

#include "include/cef_parser.h"

#include <windows.h>

#include <string>

namespace Framework::GUI::CEF {

    namespace {
        // --enable-features and --disable-features each take one comma-separated
        // list, and a second AppendSwitchWithValue shadows the first
        void AppendFeatures(const CefRefPtr<CefCommandLine> &commandLine, const char *switchName, const std::string &features) {
            std::string value = commandLine->HasSwitch(switchName) ? commandLine->GetSwitchValue(switchName).ToString() : std::string();
            if (!value.empty()) {
                value += ",";
            }
            value += features;
            commandLine->AppendSwitchWithValue(switchName, value);
        }
    } // namespace

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
        if (!_gpuAccelerated) {
            // CPU OSR path never touches the driver; a crashing GPU process otherwise
            // takes the whole browser down ("GPU process isn't usable")
            commandLine->AppendSwitch("disable-gpu");
            commandLine->AppendSwitch("disable-gpu-compositing");
        }
        else {
            // The GPU process is its own process, so the launcher's forcing does not
            // reach it and it would pick the low-power adapter. A mismatch is silent:
            // the shared texture belongs to the other adapter and never opens.
            commandLine->AppendSwitch("force-high-performance-gpu");
        }
        commandLine->AppendSwitch("disable-extensions");
        commandLine->AppendSwitch("disable-pdf-extension");
        commandLine->AppendSwitch("disable-spell-checking");
        commandLine->AppendSwitch("disable-component-update");
        AppendFeatures(commandLine, "disable-features", "WebUSB,WebHID");
        // The network service runs in this process. Out of process it is the one
        // unsandboxed child Chromium puts in a kill-on-close job, and the one
        // helper that opens sockets, which is what job policies and security
        // software single out: shipped builds have had it exit as it launched
        // and be restarted every 27 ms for the whole session, menu blank, while
        // the renderer and GPU helpers ran fine. In-process there is nothing to
        // launch, kill or police, and the UI's traffic is local resources anyway.
        AppendFeatures(commandLine, "enable-features", "NetworkServiceInProcess2");
        // Allow UI audio without a user gesture: CEF views are host-driven overlays (notifications,
        // HUD, menus) that never receive a real "user activation", so the default autoplay policy
        // would silently block all sound (Web Audio / <audio>). This opts the embedded browser out.
        commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        // No internal begin-frame scheduler: rendering uses external begin frames.
    }

    void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        Resources::RegisterCustomSchemes(registrar);
    }

    void App::OnContextInitialized() {
        _contextInitialized = true;
    }

    void App::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> commandLine) {
        // Handed over on the command line so the child never asks the kernel who launched it
        commandLine->AppendSwitchWithValue(kParentProcessSwitch, std::to_string(GetCurrentProcessId()));
    }

    void App::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        // In --single-process mode the renderer runs inside the browser process,
        // so App (not RendererApp) receives this callback. Register the same
        // JS bindings that RendererApp::OnContextCreated registers in multi-process mode.
        CefRefPtr<CefV8Value> global    = context->GetGlobal();
        CefRefPtr<CefV8Handler> handler = new CallEventHandler(browser);
        CefRefPtr<CefV8Value> func      = CefV8Value::CreateFunction("callEvent", handler);
        global->SetValue("callEvent", func, V8_PROPERTY_ATTRIBUTE_NONE);
    }

    void App::RegisterSchemeHandlerFactory(const std::string &scheme, const std::string &domain, SchemaHandlerFactoryCallback callback) {
        std::scoped_lock lock(_handlersMutex);
        _handlers[{scheme, domain}] = std::move(callback);
    }
} // namespace Framework::GUI::CEF
