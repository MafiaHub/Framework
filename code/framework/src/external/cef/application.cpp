/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "application.h"

namespace Framework::External::CEF {
    void Application::InvokeEvent(const std::string &name, const std::string &data) {
        // Construct list of arguments
        CefV8ValueList args;
        args.push_back(CefV8Value::CreateString(name));
        args.push_back(CefV8Value::CreateString(data));

        // Trigger every handlers
        for (auto &event : _scriptingEvents) { event.second->ExecuteFunctionWithContext(event.first, nullptr, args); }
    }
    void Application::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        // TODO: make this customizable
        registrar->AddCustomScheme("framework", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    void Application::OnContextInitialized() {}

    void Application::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {}

    void Application::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        if (_scriptingEvents.empty()) {
            return;
        }

        // Erase the scripting events that only belongs to this context
        auto it = _scriptingEvents.begin();
        while (it != _scriptingEvents.end()) {
            if (it->first->IsSame(context)) {
                it = _scriptingEvents.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Application::OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) {
        commandLine->AppendSwitch("enable-experimental-web-platform-features");
        commandLine->AppendSwitch("transparent-painting-enabled");
        commandLine->AppendSwitch("off-screen-rendering-enabled");
        commandLine->AppendSwitch("disable-gpu-compositing");
        commandLine->AppendSwitch("enable-begin-frame-scheduling");
        commandLine->AppendSwitch("ignore-gpu-blacklist");
        commandLine->AppendSwitch("ignore-gpu-blocklist");
    }

    bool Application::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) {
        return true;
    }

    bool Application::Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) {
        // TODO: process event handlers from the web code
        return true;
    }
} // namespace Framework::External::CEF
