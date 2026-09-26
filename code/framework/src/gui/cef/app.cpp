/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <utils/safe_win32.h>

#include "app.h"

#include "gui/resources/scheme.h"

#include "include/cef_parser.h"

#include <string>

namespace Framework::GUI::CEF {
    namespace {
        bool RunningUnderWine() {
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            return ntdll && GetProcAddress(ntdll, "wine_get_version");
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
        if (!processType.empty()) {
            return;
        }

        const bool wineSoftwareRendering = !_gpuAccelerated && RunningUnderWine();
        commandLine->AppendSwitch("disable-gpu-compositing");
        if (!_gpuAccelerated) {
            // Use CPU painting for off-screen rendering.
            commandLine->AppendSwitch("disable-gpu");
            if (wineSoftwareRendering) {
                // Wine's D3D11/ANGLE and SwiftShader paths can both fail during
                // Viz initialization. This UI only needs CPU OnPaint buffers.
                commandLine->AppendSwitch("disable-gpu-rasterization");
                commandLine->AppendSwitch("disable-software-rasterizer");
                commandLine->AppendSwitch("disable-3d-apis");
                commandLine->AppendSwitch("disable-webgl");
                commandLine->AppendSwitch("disable-gpu-process-prelaunch");
                // Chromium may still start a GPU service for software
                // compositing. Keep it in the browser under Wine.
                commandLine->AppendSwitch("in-process-gpu");
            }
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
        commandLine->AppendSwitchWithValue("disable-features", "WebUSB,WebHID");
        // Allow UI audio without a user gesture: CEF views are host-driven overlays (notifications,
        // HUD, menus) that never receive a real "user activation", so the default autoplay policy
        // would silently block all sound (Web Audio / <audio>). This opts the embedded browser out.
        commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        // No internal begin-frame scheduler: rendering uses external begin frames.
    }

    void App::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> commandLine) {
        // Chromium helpers can outlive a game that crashes or is terminated.
        // Give every helper the browser process ID explicitly: the OS-reported
        // immediate parent may be another helper (or Wine's process manager).
        commandLine->AppendSwitchWithValue("framework-browser-pid", std::to_string(GetCurrentProcessId()));
        if (!_gpuAccelerated && RunningUnderWine()) {
            // The browser process receives these in OnBeforeCommandLineProcessing,
            // but CEF does not carry them over to every child command line.
            commandLine->AppendSwitch("disable-gpu");
            commandLine->AppendSwitch("disable-gpu-compositing");
            commandLine->AppendSwitch("disable-gpu-rasterization");
            commandLine->AppendSwitch("disable-software-rasterizer");
            commandLine->AppendSwitch("disable-3d-apis");
            commandLine->AppendSwitch("disable-webgl");
        }
    }

    void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        Resources::RegisterCustomSchemes(registrar);
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
