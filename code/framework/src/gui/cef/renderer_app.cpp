/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderer_app.h"

#include "gui/resources/scheme.h"

#include "include/base/cef_logging.h"

#include <string>
#include <utility>

namespace Framework::GUI::CEF {
    void RendererApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        // Identical to App's on purpose: a scheme missing from one process of a
        // CEF instance is unusable there.
        Resources::RegisterCustomSchemes(registrar);
    }

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

    namespace {
        // The DOM is only reachable in the renderer, so the browser process cannot decide this for itself.
        // Every report is sent: the view dedupes, and resets per document, which a renderer can't see
        // across a cross-process navigation.
        void ReportFocusedNode(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDOMNode> node) {
            const bool element     = node && node->GetType() == CefDOMNode::Type::DOM_NODE_TYPE_ELEMENT;
            const bool formControl = element && node->GetFormControlElementType() != CefDOMNode::FormControlType::DOM_FORM_CONTROL_TYPE_UNSUPPORTED;
            // Text inputs, textareas and contenteditable: where a keystroke becomes a character.
            const bool editable = element && node->IsEditable();

            auto message = CefProcessMessage::Create("InputFocus");
            message->GetArgumentList()->SetBool(0, formControl);
            message->GetArgumentList()->SetBool(1, editable);
            if (auto mainFrame = browser->GetMainFrame()) {
                mainFrame->SendProcessMessage(PID_BROWSER, message);
            }
        }

        class FocusedNodeVisitor final: public CefDOMVisitor {
            CefRefPtr<CefBrowser> _browser;

          public:
            explicit FocusedNodeVisitor(CefRefPtr<CefBrowser> browser): _browser(std::move(browser)) {}

            void Visit(CefRefPtr<CefDOMDocument> document) override {
                ReportFocusedNode(_browser, document->GetFocusedNode());
            }

            IMPLEMENT_REFCOUNTING(FocusedNodeVisitor);
        };

        // An element made editable while it holds focus moves no focus, so OnFocusedNodeChanged never
        // hears of it. The page's observer calls this instead; it is never put on window.
        class FocusRecheckHandler final: public CefV8Handler {
          public:
            bool Execute(const CefString &, CefRefPtr<CefV8Value>, const CefV8ValueList &, CefRefPtr<CefV8Value> &, CefString &) override {
                const auto context = CefV8Context::GetCurrentContext();
                const auto frame   = context ? context->GetFrame() : nullptr;
                const auto browser = context ? context->GetBrowser() : nullptr;
                if (frame && browser) {
                    frame->VisitDOM(new FocusedNodeVisitor(browser));
                }
                return true;
            }

            IMPLEMENT_REFCOUNTING(FocusRecheckHandler);
        };

        // Re-reads the focused element when it or an ancestor changes what makes it editable. Focus
        // inside a child frame is that frame's own observer's to report.
        constexpr const char *kFocusRecheckObserver = R"js(((recheck) => {
            new MutationObserver((records) => {
                const active = document.activeElement;
                if (active && active.localName !== "iframe" && records.some((record) => record.target.contains(active))) recheck();
            }).observe(document, { subtree: true, attributes: true, attributeFilter: ["contenteditable", "type"] });
        }))js";
    } // namespace

    void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        CefRefPtr<CefV8Value> global    = context->GetGlobal();
        CefRefPtr<CefV8Handler> handler = new CallEventHandler(browser);
        CefRefPtr<CefV8Value> func      = CefV8Value::CreateFunction("callEvent", handler);
        global->SetValue("callEvent", func, V8_PROPERTY_ATTRIBUTE_NONE);

        CefRefPtr<CefV8Value> observe;
        CefRefPtr<CefV8Exception> error;
        const bool evaluated = context->Eval(kFocusRecheckObserver, CefString(), 0, observe, error) && observe && observe->IsFunction();
        if (!evaluated || !observe->ExecuteFunctionWithContext(context, nullptr, {CefV8Value::CreateFunction("recheck", new FocusRecheckHandler())})) {
            // Without it a field made editable in place is never reported, and push-to-talk stays open over it.
            LOG(ERROR) << "Focus re-check observer not installed in " << (frame ? frame->GetURL().ToString() : std::string {}) << ": " << (error ? error->GetMessage().ToString() : std::string {"no observer function"});
        }
    }

    void RendererApp::OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefDOMNode> node) {
        if (browser) {
            ReportFocusedNode(browser, node);
        }
    }
} // namespace Framework::GUI::CEF
