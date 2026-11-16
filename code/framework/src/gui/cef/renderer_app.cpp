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

#include <algorithm>
#include <cctype>
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
        // From the frame whose script called: the browser process checks that frame's origin
        // against the view's. Sending from the main frame would vouch for every embedded frame,
        // and in a cross-origin child frame's process there is no main frame to send from.
        CefRefPtr<CefV8Context> caller = CefV8Context::GetCurrentContext();
        CefRefPtr<CefFrame> frame      = caller ? caller->GetFrame() : nullptr;
        if (!frame) {
            exception = "callEvent is not available in this frame";
            return true;
        }
        frame->SendProcessMessage(PID_BROWSER, msg);

        retval = CefV8Value::CreateBool(true);
        return true;
    }

    namespace {
        bool IsFrameElement(CefRefPtr<CefDOMNode> node) {
            std::string tag = node->GetElementTagName().ToString();
            std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return tag == "iframe" || tag == "frame";
        }

        // The DOM is only reachable in the renderer, so the browser process cannot decide this for itself.
        // Every report is sent: the view dedupes, and resets per document, which a renderer can't see
        // across a cross-process navigation.
        void ReportFocusedNode(CefRefPtr<CefFrame> frame, CefRefPtr<CefDOMNode> node) {
            const bool element = node && node->GetType() == CefDOMNode::Type::DOM_NODE_TYPE_ELEMENT;
            // A focused frame element hands the keys to the document inside it, which reports for
            // itself, from its own process when cross-origin. Reporting the frame element as well
            // would race that report, and arriving last would reopen push-to-talk over a text field.
            if (element && IsFrameElement(node)) {
                return;
            }
            const bool formControl = element && node->GetFormControlElementType() != CefDOMNode::FormControlType::DOM_FORM_CONTROL_TYPE_UNSUPPORTED;
            // Text inputs, textareas, contenteditable and role="textbox": where a keystroke becomes a
            // character. A read-only control takes the caret but no characters.
            const bool editable = element && node->IsEditable() && !(formControl && node->HasElementAttribute("readonly"));

            auto message = CefProcessMessage::Create("InputFocus");
            message->GetArgumentList()->SetBool(0, formControl);
            message->GetArgumentList()->SetBool(1, editable);
            // From the reporting frame: in a cross-origin child frame's process the main frame is
            // remote, and GetMainFrame returns null there.
            frame->SendProcessMessage(PID_BROWSER, message);
        }

        class FocusedNodeVisitor final: public CefDOMVisitor {
            CefRefPtr<CefFrame> _frame;

          public:
            explicit FocusedNodeVisitor(CefRefPtr<CefFrame> frame): _frame(std::move(frame)) {}

            void Visit(CefRefPtr<CefDOMDocument> document) override {
                ReportFocusedNode(_frame, document->GetFocusedNode());
            }

            IMPLEMENT_REFCOUNTING(FocusedNodeVisitor);
        };

        // An element made editable while it holds focus moves no focus, so OnFocusedNodeChanged never
        // hears of it. The page's observer and key listener call this instead; it is never put on window.
        class FocusRecheckHandler final: public CefV8Handler {
          public:
            bool Execute(const CefString &, CefRefPtr<CefV8Value>, const CefV8ValueList &, CefRefPtr<CefV8Value> &, CefString &) override {
                const auto context = CefV8Context::GetCurrentContext();
                if (const auto frame = context ? context->GetFrame() : nullptr) {
                    frame->VisitDOM(new FocusedNodeVisitor(frame));
                }
                return true;
            }

            IMPLEMENT_REFCOUNTING(FocusRecheckHandler);
        };

        // Re-reads the focused element when it or an ancestor changes an attribute ReportFocusedNode reads;
        // a child frame reports its own. Shadow roots, designMode and CSS escape the observer, so each key
        // re-reads it too: keys cross shadow boundaries, and this listener precedes the page's.
        constexpr const char *kFocusRecheckObserver = R"js(((recheck) => {
            new MutationObserver((records) => {
                const active = document.activeElement;
                if (active && active.localName !== "iframe" && active.localName !== "frame" && records.some((record) => record.target.contains(active))) recheck();
            }).observe(document, { subtree: true, attributes: true, attributeFilter: ["contenteditable", "type", "role", "readonly"] });
            window.addEventListener("keydown", () => recheck(), true);
        }))js";
    } // namespace

    void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        CefRefPtr<CefV8Value> global    = context->GetGlobal();
        CefRefPtr<CefV8Handler> handler = new CallEventHandler();
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
        if (frame) {
            ReportFocusedNode(frame, node);
        }
    }
} // namespace Framework::GUI::CEF
