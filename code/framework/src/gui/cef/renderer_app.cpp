/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderer_app.h"

namespace Framework::GUI::CEF {
    bool CallEventHandler::Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) {
        if (arguments.size() != 2) {
            exception = "Invalid argument count: callEvent(string, string | null)";
            return true;
        }

        if (!arguments[0]->IsString()) {
            exception = "First argument must be a string";
            return true;
        }

        CefString eventName = arguments[0]->GetStringValue();
        CefString payload;

        if (arguments[1]->IsString()) {
            payload = arguments[1]->GetStringValue();
        }
        else if (!arguments[1]->IsNull() && !arguments[1]->IsUndefined()) {
            exception = "Second argument must be a string or null";
            return true;
        }

        auto msg  = CefProcessMessage::Create("CallEvent");
        auto args = msg->GetArgumentList();
        args->SetString(0, eventName);
        args->SetString(1, payload);
        _browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);

        retval = CefV8Value::CreateBool(true);
        return true;
    }

    void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        CefRefPtr<CefV8Value> global  = context->GetGlobal();
        CefRefPtr<CefV8Handler> handler = new CallEventHandler(browser);
        CefRefPtr<CefV8Value> func    = CefV8Value::CreateFunction("callEvent", handler);
        global->SetValue("callEvent", func, V8_PROPERTY_ATTRIBUTE_NONE);
    }
} // namespace Framework::GUI::CEF
