#include "sdk.h"

#include <logging/logger.h>

#include <JavaScriptCorePP/JSHelper.h>

namespace Framework::GUI {
    bool SDK::Init(ultralight::View* caller) {
        // Bind the view reference
        _view = caller;

        // Initialize our context
        auto context = _view->LockJSContext();
        _context = (*context);

        // Bind our global object
        _globalObject = JSContextGetGlobalObject(_context);

        // Initialize the builtin SDK
        InitEventHandlers();
        return true;
    }

    bool SDK::Shutdown() {
        return true;
    }

    void SDK::InitEventHandlers() {
        // Grab the context
        auto context = JavaScriptCorePP::JSContext(_context);
        auto obj = context.GetGlobalObject();

        // Bind the callEvent method
        obj["callEvent"] = [this](const JavaScriptCorePP::JSContext &context, const std::vector<JavaScriptCorePP::JSValue> &args, JavaScriptCorePP::JSValue &returnValue, JavaScriptCorePP::JSValue &returnException) {
            // Make sure there is only two arguments
            if (args.size() != 2) {
                returnException = context.CreateString("Invalid argument count: callEvent(string, string | null)");
                return;
            }

            std::string eventName;
            std::string eventPayload;

            // Grab the event name - must be a string
            if (args[0].IsString()) {
                eventName = args[0].GetString();
            }
            else {
                returnException = context.CreateString("First argument must be a string");
                return;
            }

            // Grab the event payload - must be a string or null
            if (args[1].IsString()) {
                eventPayload = args[1].GetString();
            }
            else if (args[1].IsNull() || args[1].IsUndefined()) {
                eventPayload = "";
            }
            else {
                returnException = context.CreateString("Second argument must be a string or null");
                return;
            }

            // Process all event callbacks
            BroadcastEvent(eventName, eventPayload);
            returnValue = context.CreateBoolean(true);
        };
    }
}
