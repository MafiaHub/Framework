/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "web.h"

#include <core_modules.h>

#include <gui/manager.h>
#include <gui/view.h>

#include <scripting/builtins/events.h>
#include <scripting/engine.h>
#include <scripting/engine_helpers.h>
#include <scripting/resource/resource.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>

#include <logging/logger.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <tuple>
#include <utility>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        // Keep in sync with the payload builder, the catalog metadata, and the docs.
        constexpr std::array<std::pair<Framework::GUI::ViewEvent, std::string_view>, 12> kViewEventNames {{
            {Framework::GUI::ViewEvent::Created, "browserCreated"},
            {Framework::GUI::ViewEvent::LoadingStart, "browserLoadingStart"},
            {Framework::GUI::ViewEvent::DocumentReady, "browserDocumentReady"},
            {Framework::GUI::ViewEvent::LoadingFailed, "browserLoadingFailed"},
            {Framework::GUI::ViewEvent::Navigate, "browserNavigate"},
            {Framework::GUI::ViewEvent::Popup, "browserPopup"},
            {Framework::GUI::ViewEvent::CursorChange, "browserCursorChange"},
            {Framework::GUI::ViewEvent::Tooltip, "browserTooltip"},
            {Framework::GUI::ViewEvent::InputFocusChange, "browserInputFocusChange"},
            {Framework::GUI::ViewEvent::ResourceBlocked, "browserResourceBlocked"},
            {Framework::GUI::ViewEvent::ConsoleMessage, "browserConsoleMessage"},
            {Framework::GUI::ViewEvent::OriginChange, "browserOriginChange"},
        }};

        std::string_view ViewEventName(Framework::GUI::ViewEvent event) {
            for (const auto &[value, name] : kViewEventNames) {
                if (value == event) {
                    return name;
                }
            }
            return {};
        }

        const char *BlockReasonName(Framework::GUI::ViewBlockReason reason) {
            switch (reason) {
            case Framework::GUI::ViewBlockReason::CrossOrigin: return "cross-origin";
            case Framework::GUI::ViewBlockReason::InvalidURL: return "invalid-url";
            case Framework::GUI::ViewBlockReason::HostFilter: return "host-filter";
            case Framework::GUI::ViewBlockReason::ForeignEvent: return "foreign-event";
            }
            return "unknown";
        }

        // Panning and drag-and-drop shapes have no CSS name; they report as "custom".
        const char *CursorTypeName(int type) {
            switch (static_cast<cef_cursor_type_t>(type)) {
            case CT_POINTER: return "default";
            case CT_CROSS: return "crosshair";
            case CT_HAND: return "pointer";
            case CT_IBEAM: return "text";
            case CT_WAIT: return "wait";
            case CT_HELP: return "help";
            case CT_EASTRESIZE: return "e-resize";
            case CT_NORTHRESIZE: return "n-resize";
            case CT_NORTHEASTRESIZE: return "ne-resize";
            case CT_NORTHWESTRESIZE: return "nw-resize";
            case CT_SOUTHRESIZE: return "s-resize";
            case CT_SOUTHEASTRESIZE: return "se-resize";
            case CT_SOUTHWESTRESIZE: return "sw-resize";
            case CT_WESTRESIZE: return "w-resize";
            case CT_NORTHSOUTHRESIZE: return "ns-resize";
            case CT_EASTWESTRESIZE: return "ew-resize";
            case CT_NORTHEASTSOUTHWESTRESIZE: return "nesw-resize";
            case CT_NORTHWESTSOUTHEASTRESIZE: return "nwse-resize";
            case CT_COLUMNRESIZE: return "col-resize";
            case CT_ROWRESIZE: return "row-resize";
            case CT_MOVE: return "move";
            case CT_VERTICALTEXT: return "vertical-text";
            case CT_CELL: return "cell";
            case CT_CONTEXTMENU: return "context-menu";
            case CT_ALIAS: return "alias";
            case CT_PROGRESS: return "progress";
            case CT_NODROP: return "no-drop";
            case CT_COPY: return "copy";
            case CT_NONE: return "none";
            case CT_NOTALLOWED: return "not-allowed";
            case CT_ZOOMIN: return "zoom-in";
            case CT_ZOOMOUT: return "zoom-out";
            case CT_GRAB: return "grab";
            case CT_GRABBING: return "grabbing";
            default: return "custom";
            }
        }

        const char *ConsoleSeverityName(int severity) {
            switch (static_cast<cef_log_severity_t>(severity)) {
            case LOGSEVERITY_DEBUG: return "debug";
            case LOGSEVERITY_WARNING: return "warning";
            case LOGSEVERITY_ERROR: return "error";
            case LOGSEVERITY_FATAL: return "fatal";
            default: return "info";
            }
        }

        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        // Handler origin, explicit context, then V8 stack — same order as Events.
        std::string ResolveResourceName(v8::Isolate *isolate, Framework::Scripting::ResourceManager *manager, v8::Local<v8::Function> fn = {}) {
            if (!fn.IsEmpty()) {
                std::string name = manager->GetResourceNameFromFunction(isolate, fn);
                if (!name.empty()) {
                    return name;
                }
            }
            std::string name = manager->GetCurrentResourceContext();
            if (!name.empty()) {
                return name;
            }
            return manager->GetResourceContextFromStack(isolate);
        }

        int GetIntOption(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> obj, const char *key, int defaultValue) {
            v8::Local<v8::Value> value;
            if (!obj->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value) || !value->IsNumber()) {
                return defaultValue;
            }
            return value->Int32Value(context).FromMaybe(defaultValue);
        }

        bool GetBoolOption(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> obj, const char *key, bool defaultValue) {
            v8::Local<v8::Value> value;
            if (!obj->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value) || !value->IsBoolean()) {
                return defaultValue;
            }
            return value->BooleanValue(isolate);
        }

        bool GetViewIdArg(const v8::FunctionCallbackInfo<v8::Value> &args, const char *methodName, int &outId) {
            v8::Isolate *isolate = args.GetIsolate();
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                ThrowError(isolate, std::string(methodName) + ": first argument must be a view id");
                return false;
            }
            outId = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1);
            return true;
        }

        std::string EscapeJSString(const std::string &input) {
            std::string out;
            out.reserve(input.size());
            for (const char c : input) {
                switch (c) {
                case '\\': out += "\\\\"; break;
                case '\'': out += "\\'"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else {
                        out += c;
                    }
                }
            }
            return out;
        }

        v8::Local<v8::Object> BuildViewEventPayload(v8::Isolate *isolate, v8::Local<v8::Context> context, int viewId, const Framework::GUI::ViewEventData &data) {
            v8::Local<v8::Object> payload = v8::Object::New(isolate);
            const auto set                = [&](const char *key, v8::Local<v8::Value> value) {
                payload->Set(context, v8pp::to_v8(isolate, key), value).Check();
            };
            const auto setString = [&](const char *key, const std::string &value) {
                set(key, v8pp::to_v8(isolate, value));
            };

            set("viewId", v8pp::to_v8(isolate, viewId));

            switch (data.event) {
            case Framework::GUI::ViewEvent::Created:
                setString("url", data.url);
                break;
            case Framework::GUI::ViewEvent::LoadingStart:
                setString("url", data.url);
                set("isMainFrame", v8pp::to_v8(isolate, data.isMainFrame));
                break;
            case Framework::GUI::ViewEvent::DocumentReady:
                setString("url", data.url);
                break;
            case Framework::GUI::ViewEvent::LoadingFailed:
                setString("url", data.url);
                setString("description", data.description);
                set("errorCode", v8pp::to_v8(isolate, data.errorCode));
                set("isMainFrame", v8pp::to_v8(isolate, data.isMainFrame));
                break;
            case Framework::GUI::ViewEvent::Navigate:
                setString("url", data.url);
                set("isMainFrame", v8pp::to_v8(isolate, data.isMainFrame));
                set("blocked", v8pp::to_v8(isolate, data.blocked));
                break;
            case Framework::GUI::ViewEvent::Popup:
                setString("url", data.url);
                setString("openerUrl", data.openerUrl);
                break;
            case Framework::GUI::ViewEvent::CursorChange:
                setString("cursor", CursorTypeName(data.cursorType));
                set("cursorType", v8pp::to_v8(isolate, data.cursorType));
                break;
            case Framework::GUI::ViewEvent::Tooltip:
                setString("text", data.tooltip);
                break;
            case Framework::GUI::ViewEvent::InputFocusChange:
                set("focused", v8pp::to_v8(isolate, data.focused));
                break;
            case Framework::GUI::ViewEvent::ResourceBlocked:
                setString("url", data.url);
                setString("domain", data.domain);
                setString("reason", BlockReasonName(data.reason));
                break;
            case Framework::GUI::ViewEvent::ConsoleMessage:
                setString("message", data.message);
                setString("source", data.source);
                set("line", v8pp::to_v8(isolate, data.line));
                setString("severity", ConsoleSeverityName(data.severity));
                break;
            case Framework::GUI::ViewEvent::OriginChange:
                setString("origin", data.origin);
                setString("url", data.url);
                break;
            }

            return payload;
        }
    } // anonymous namespace

    std::map<int, std::map<std::string, std::vector<Web::Handler>>> Web::_handlers;
    std::map<int, std::string> Web::_viewOwners;
    std::deque<Web::QueuedViewEvent> Web::_pendingViewEvents;
    std::mutex Web::_mutex;
    Framework::Scripting::ResourceManager *Web::_resourceManager = nullptr;

    void Web::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        {
            std::scoped_lock lock(_mutex);
            _handlers.clear();
            _viewOwners.clear();
            _pendingViewEvents.clear();
        }
        _resourceManager = resourceManager;

        v8::Local<v8::Object> webObj = v8::Object::New(isolate);

        const auto bind = [&](const char *name, v8::FunctionCallback callback) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback);
            webObj->Set(context, v8pp::to_v8(isolate, name), tmpl->GetFunction(context).ToLocalChecked()).Check();
        };

        bind("createView", CreateViewCallback);
        bind("destroyView", DestroyViewCallback);
        bind("showView", ShowViewCallback);
        bind("hideView", HideViewCallback);
        bind("focusView", FocusViewCallback);
        bind("isViewVisible", IsViewVisibleCallback);
        bind("loadURL", LoadURLCallback);
        bind("resizeView", ResizeViewCallback);
        bind("setViewPosition", SetViewPositionCallback);
        bind("on", OnCallback);
        bind("off", OffCallback);
        bind("emit", EmitCallback);
        bind("getScreenSize", GetScreenSizeCallback);

        target->Set(context, v8pp::to_v8(isolate, "Web"), webObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Web", "Client-only, resource-owned CEF web-view API exposed as the global Web.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("createView",
            v8pp::metadata::docs("number",
                {v8pp::metadata::param("url", "string", false, "Resource-relative URL or allowed absolute URL loaded into the view."),
                    v8pp::metadata::param("options", "{ width?: number; height?: number; x?: number; y?: number; zIndex?: number; visible?: boolean; focus?: boolean }", true, "Optional initial pixel geometry, stacking, visibility, and focus settings.")},
                "Creates an origin-locked web view owned by the calling resource.", "Numeric view ID used by the remaining Web methods.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("destroyView",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("viewId", "number", false, "Owned view identifier.")}, "Destroys an owned view and removes all of its script event handlers.", "True when a view was destroyed.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("showView", v8pp::metadata::docs("boolean", {v8pp::metadata::param("viewId", "number", false, "Owned view identifier.")}, "Makes an owned view visible.", "True when the view exists and was updated.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("hideView", v8pp::metadata::docs("boolean", {v8pp::metadata::param("viewId", "number", false, "Owned view identifier.")}, "Hides an owned view.", "True when the view exists and was updated.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("focusView",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("focused", "boolean", true, "Whether the view captures keyboard and mouse input; defaults to true.")},
                "Changes input focus for an owned view.", "True when the view exists and focus was updated.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isViewVisible",
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("viewId", "number", false, "Owned view identifier.")}, "Checks whether an owned view is currently visible.", "False for missing or unowned views.")));
        metadata.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("loadURL", v8pp::metadata::docs("boolean", {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("url", "string", false, "New resource-relative or allowed absolute URL.")},
                                                                             "Navigates a view and replaces its allowed origin with the new URL's origin.", "True when navigation was requested.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("resizeView",
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("width", "number", false, "New viewport width in pixels."), v8pp::metadata::param("height", "number", false, "New viewport height in pixels.")},
                "Resizes an owned view's viewport.", "True when the view exists and was resized.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setViewPosition",
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("x", "number", false, "New horizontal screen position in pixels."), v8pp::metadata::param("y", "number", false, "New vertical screen position in pixels.")},
                "Moves an owned view on screen.", "True when the view exists and was moved.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("on", v8pp::metadata::docs("void",
                                                                                    {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("eventName", "string", false, "Page-to-script event name."),
                                                                                        v8pp::metadata::param("handler", "(payload: unknown) => void", false, "Resource-owned callback invoked by the view's callEvent bridge.")},
                                                                                    "Registers a handler for an event emitted by an owned same-origin page.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("off", v8pp::metadata::docs("boolean",
                                                                                     {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("eventName", "string", false, "Page-to-script event name."),
                                                                                         v8pp::metadata::param("handler", "(payload: unknown) => void", true, "Optional exact callback; omitting it removes every matching handler owned by the resource.")},
                                                                                     "Removes page-event handlers from an owned view.", "True when at least one handler was removed.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emit", v8pp::metadata::docs("boolean",
                                                                                      {v8pp::metadata::param("viewId", "number", false, "Owned view identifier."), v8pp::metadata::param("eventName", "string", false, "CustomEvent name dispatched in the page."),
                                                                                          v8pp::metadata::param("payload", "unknown", true, "Optional JSON-serializable event detail.")},
                                                                                      "Dispatches a CustomEvent into an owned view.", "True when the dispatch script was queued.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("getScreenSize", v8pp::metadata::docs("{ width: number; height: number }", {}, "Returns the current client viewport size.", "Width and height in physical pixels.")));

        RegisterViewEventMetadata(isolate);
    }

    void Web::RegisterViewEventMetadata(v8::Isolate *isolate) {
        auto &catalog = Framework::Scripting::GetScriptingCatalog(isolate);
        auto &events  = catalog.data_type("EventMap", "Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.");

        const auto describe = [&](Framework::GUI::ViewEvent event, const char *typeName, const char *typeDescription, const std::vector<std::tuple<const char *, const char *, const char *>> &fields, const char *eventDescription) {
            auto &payload = catalog.data_type(typeName, typeDescription);
            payload.add_property("viewId", "number", "Identifier of the view the event belongs to.");
            for (const auto &[name, type, description] : fields) {
                payload.add_property(name, type, description);
            }
            events.add_property(std::string(ViewEventName(event)), std::string("[event: ") + typeName + "]", eventDescription);
        };

        describe(Framework::GUI::ViewEvent::Created, "BrowserCreatedEvent", "A web view's browser finished being created.", {{"url", "string", "URL the view was created with."}},
            "Dispatched once per view, before any navigation, to the resource that created it.");
        describe(Framework::GUI::ViewEvent::LoadingStart, "BrowserLoadingStartEvent", "A frame inside a web view started loading.",
            {{"url", "string", "URL being loaded."}, {"isMainFrame", "boolean", "False for sub-frame loads."}}, "Dispatched when any frame of an owned view begins loading.");
        describe(Framework::GUI::ViewEvent::DocumentReady, "BrowserDocumentReadyEvent", "A web view's main frame finished loading.", {{"url", "string", "URL that finished loading."}},
            "Dispatched when an owned view's main document is ready; the earliest point at which the page can receive Web.emit.");
        describe(Framework::GUI::ViewEvent::LoadingFailed, "BrowserLoadingFailedEvent", "A load inside a web view was aborted.",
            {{"url", "string", "URL that failed to load."}, {"description", "string", "CEF error text, such as ERR_CONNECTION_REFUSED."}, {"errorCode", "number", "CEF error code."}, {"isMainFrame", "boolean", "False for sub-frame failures."}},
            "Dispatched when a frame of an owned view fails to load.");
        describe(Framework::GUI::ViewEvent::Navigate, "BrowserNavigateEvent", "A web view was asked to navigate.",
            {{"url", "string", "Requested URL."}, {"isMainFrame", "boolean", "False for sub-frame navigation."}, {"blocked", "boolean", "Whether the request was refused."}},
            "Dispatched for every navigation an owned view is asked to perform, refused or not.");
        describe(Framework::GUI::ViewEvent::Popup, "BrowserPopupEvent", "A page tried to open a new window or tab.", {{"url", "string", "Target URL of the blocked popup."}, {"openerUrl", "string", "URL of the frame that requested it."}},
            "Dispatched when an owned view blocks a popup; windowless views cannot host one, so handle the URL yourself.");
        describe(Framework::GUI::ViewEvent::CursorChange, "BrowserCursorChangeEvent", "The cursor shape a page is asking for.",
            {{"cursor", "string", "CSS-style cursor name, or \"custom\" for shapes without one."}, {"cursorType", "number", "Raw CEF cursor type."}}, "Dispatched when an owned view's requested cursor shape changes.");
        describe(Framework::GUI::ViewEvent::Tooltip, "BrowserTooltipEvent", "A page wants to display a tooltip.", {{"text", "string", "Tooltip text; empty when the tooltip is dismissed."}},
            "Dispatched when an owned view requests a tooltip; windowless rendering draws none, so the script must.");
        describe(Framework::GUI::ViewEvent::InputFocusChange, "BrowserInputFocusChangeEvent", "A form control inside a page gained or lost focus.", {{"focused", "boolean", "True while the page holds keyboard input."}},
            "Dispatched when focus enters or leaves an editable element of an owned view; use it to stop routing keys to the game.");
        describe(Framework::GUI::ViewEvent::ResourceBlocked, "BrowserResourceBlockedEvent", "A web view refused a request.",
            {{"url", "string", "URL that was refused."}, {"domain", "string", "Host component of that URL, empty when unparsable."}, {"reason", "\"cross-origin\" | \"invalid-url\" | \"host-filter\" | \"foreign-event\"", "Why the request was refused."}},
            "Dispatched when an owned view rejects a navigation or a page event from outside its locked origin.");
        describe(Framework::GUI::ViewEvent::ConsoleMessage, "BrowserConsoleMessageEvent", "A console call made by a page.",
            {{"message", "string", "Console message body."}, {"source", "string", "Script URL that logged it."}, {"line", "number", "Line number within that script."}, {"severity", "\"debug\" | \"info\" | \"warning\" | \"error\" | \"fatal\"", "Console severity."}},
            "Dispatched for console output of an owned view; the framework logs these regardless.");
        describe(Framework::GUI::ViewEvent::OriginChange, "BrowserOriginChangeEvent", "A web view's allowed origin changed.", {{"origin", "string", "New locked origin, or \"null\" when the URL has none."}, {"url", "string", "URL the lock was derived from."}},
            "Dispatched on view creation and whenever Web.loadURL re-locks an owned view to a different origin.");
    }

    Framework::GUI::View *Web::GetOwnedView(int viewId) {
        {
            std::scoped_lock lock(_mutex);
            if (!_viewOwners.contains(viewId)) {
                return nullptr;
            }
        }
        auto *gui = CoreModules::GetWebManager();
        if (!gui) {
            return nullptr;
        }
        return gui->GetView(viewId);
    }

    void Web::CreateViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Web.createView requires a url string");
            return;
        }

        if (!_resourceManager) {
            ThrowError(isolate, "Web.createView: resource manager not available");
            return;
        }

        const std::string owner = ResolveResourceName(isolate, _resourceManager);
        if (owner.empty()) {
            ThrowError(isolate, "Web.createView: must be called from within a resource");
            return;
        }

        auto *gui = CoreModules::GetWebManager();
        if (!gui || !gui->IsInitialized()) {
            ThrowError(isolate, "Web.createView: web view manager is not available");
            return;
        }

        const std::string url = v8pp::from_v8<std::string>(isolate, args[0]);

        int width = 0, height = 0, x = 0, y = 0, zIndex = 0;
        bool visible = true, focus = false;
        if (args.Length() >= 2 && args[1]->IsObject()) {
            v8::Local<v8::Object> options = args[1].As<v8::Object>();
            width                         = GetIntOption(isolate, context, options, "width", 0);
            height                        = GetIntOption(isolate, context, options, "height", 0);
            x                             = GetIntOption(isolate, context, options, "x", 0);
            y                             = GetIntOption(isolate, context, options, "y", 0);
            zIndex                        = GetIntOption(isolate, context, options, "zIndex", 0);
            visible                       = GetBoolOption(isolate, context, options, "visible", true);
            focus                         = GetBoolOption(isolate, context, options, "focus", false);
        }

        const int id = gui->CreateView(url, width, height, x, y);
        if (id < 0) {
            ThrowError(isolate, "Web.createView: failed to create web view");
            return;
        }

        {
            std::scoped_lock lock(_mutex);
            _viewOwners[id] = owner;
        }

        if (auto *view = gui->GetView(id)) {
            view->SetGarbageCollected(true);
            // before LockToOrigin, so its originChange is queued too
            AttachViewEvents(view, id);
            view->LockToOrigin(url);
            view->SetZIndex(zIndex);
            view->Display(visible);
            if (focus) {
                view->Focus(true);
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Created web view {} ({})", owner, id, url);
        args.GetReturnValue().Set(id);
    }

    void Web::DestroyViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        int id = -1;
        if (!GetViewIdArg(args, "Web.destroyView", id)) {
            return;
        }

        args.GetReturnValue().Set(DestroyViewInternal(id));
    }

    void Web::ShowViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        int id = -1;
        if (!GetViewIdArg(args, "Web.showView", id)) {
            return;
        }

        auto *view = GetOwnedView(id);
        if (!view) {
            args.GetReturnValue().Set(false);
            return;
        }
        view->Display(true);
        args.GetReturnValue().Set(true);
    }

    void Web::HideViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        int id = -1;
        if (!GetViewIdArg(args, "Web.hideView", id)) {
            return;
        }

        auto *view = GetOwnedView(id);
        if (!view) {
            args.GetReturnValue().Set(false);
            return;
        }
        // drop focus too, else the mod's focus-based input gate stays on
        view->Display(false);
        view->Focus(false);
        args.GetReturnValue().Set(true);
    }

    void Web::FocusViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        int id = -1;
        if (!GetViewIdArg(args, "Web.focusView", id)) {
            return;
        }

        bool focused = true;
        if (args.Length() >= 2 && args[1]->IsBoolean()) {
            focused = args[1]->BooleanValue(isolate);
        }

        auto *view = GetOwnedView(id);
        if (!view) {
            args.GetReturnValue().Set(false);
            return;
        }
        view->Focus(focused);
        args.GetReturnValue().Set(true);
    }

    void Web::IsViewVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        int id = -1;
        if (!GetViewIdArg(args, "Web.isViewVisible", id)) {
            return;
        }

        auto *view = GetOwnedView(id);
        args.GetReturnValue().Set(view != nullptr && view->ShouldDisplay());
    }

    void Web::LoadURLCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsString()) {
            ThrowError(isolate, "Web.loadURL requires 2 arguments: viewId, url");
            return;
        }

        const int id          = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1);
        const std::string url = v8pp::from_v8<std::string>(isolate, args[1]);

        auto *view = GetOwnedView(id);
        if (!view) {
            args.GetReturnValue().Set(false);
            return;
        }
        view->LoadURL(url);
        args.GetReturnValue().Set(true);
    }

    void Web::ResizeViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 3 || !args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber()) {
            ThrowError(isolate, "Web.resizeView requires 3 arguments: viewId, width, height");
            return;
        }

        const int id     = args[0]->Int32Value(context).FromMaybe(-1);
        const int width  = args[1]->Int32Value(context).FromMaybe(0);
        const int height = args[2]->Int32Value(context).FromMaybe(0);

        auto *view = GetOwnedView(id);
        if (!view || width <= 0 || height <= 0) {
            args.GetReturnValue().Set(false);
            return;
        }
        // explicit size means the view no longer tracks the viewport
        view->SetAutoResize(false);
        view->Resize(width, height);
        args.GetReturnValue().Set(true);
    }

    void Web::SetViewPositionCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 3 || !args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber()) {
            ThrowError(isolate, "Web.setViewPosition requires 3 arguments: viewId, x, y");
            return;
        }

        const int id = args[0]->Int32Value(context).FromMaybe(-1);
        const int x  = args[1]->Int32Value(context).FromMaybe(0);
        const int y  = args[2]->Int32Value(context).FromMaybe(0);

        auto *view = GetOwnedView(id);
        if (!view) {
            args.GetReturnValue().Set(false);
            return;
        }
        view->SetPosition(x, y);
        args.GetReturnValue().Set(true);
    }

    void Web::OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 3 || !args[0]->IsNumber() || !args[1]->IsString() || !args[2]->IsFunction()) {
            ThrowError(isolate, "Web.on requires 3 arguments: viewId, eventName, handler");
            return;
        }

        if (!_resourceManager) {
            ThrowError(isolate, "Web.on: resource manager not available");
            return;
        }

        const int id                    = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1);
        const std::string eventName     = v8pp::from_v8<std::string>(isolate, args[1]);
        v8::Local<v8::Function> handler = args[2].As<v8::Function>();

        if (eventName.empty()) {
            ThrowError(isolate, "Web.on: eventName must not be empty");
            return;
        }

        const std::string resourceName = ResolveResourceName(isolate, _resourceManager, handler);
        if (resourceName.empty()) {
            ThrowError(isolate, "Web.on: must be called from within a resource");
            return;
        }

        auto *view = GetOwnedView(id);
        if (!view) {
            ThrowError(isolate, "Web.on: unknown web view " + std::to_string(id));
            return;
        }

        bool firstForEvent = false;
        {
            std::scoped_lock lock(_mutex);
            auto &handlers = _handlers[id][eventName];
            firstForEvent  = handlers.empty();

            Handler entry;
            entry.callback.Reset(isolate, handler);
            entry.resourceName = resourceName;
            handlers.push_back(std::move(entry));
        }

        if (firstForEvent) {
            view->AddEventListener(eventName, [id, eventName](const std::string &payload) {
                DispatchPageEvent(id, eventName, payload);
            });
        }
    }

    void Web::OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsString()) {
            ThrowError(isolate, "Web.off requires at least 2 arguments: viewId, eventName");
            return;
        }

        const int id                = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1);
        const std::string eventName = v8pp::from_v8<std::string>(isolate, args[1]);

        v8::Local<v8::Function> handler;
        if (args.Length() >= 3 && args[2]->IsFunction()) {
            handler = args[2].As<v8::Function>();
        }

        bool removed        = false;
        bool removeListener = false;
        {
            std::scoped_lock lock(_mutex);
            auto viewIt = _handlers.find(id);
            if (viewIt != _handlers.end()) {
                auto eventIt = viewIt->second.find(eventName);
                if (eventIt != viewIt->second.end()) {
                    auto &handlers = eventIt->second;
                    if (handler.IsEmpty()) {
                        removed = !handlers.empty();
                        handlers.clear();
                    }
                    else {
                        const auto before = handlers.size();
                        handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                           [&](const Handler &h) {
                                               return h.callback.Get(isolate)->StrictEquals(handler);
                                           }),
                            handlers.end());
                        removed = handlers.size() != before;
                    }
                    if (handlers.empty()) {
                        viewIt->second.erase(eventIt);
                        removeListener = true;
                    }
                }
            }
        }

        if (removeListener) {
            if (auto *view = GetOwnedView(id)) {
                view->RemoveEventListener(eventName);
            }
        }

        args.GetReturnValue().Set(removed);
    }

    void Web::EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsString()) {
            ThrowError(isolate, "Web.emit requires at least 2 arguments: viewId, eventName");
            return;
        }

        const int id                = args[0]->Int32Value(context).FromMaybe(-1);
        const std::string eventName = v8pp::from_v8<std::string>(isolate, args[1]);

        auto *view = GetOwnedView(id);
        if (!view) {
            args.GetReturnValue().Set(false);
            return;
        }

        std::string detail;
        if (args.Length() >= 3 && !args[2]->IsUndefined()) {
            v8::Local<v8::String> jsonStr;
            if (!v8::JSON::Stringify(context, args[2]).ToLocal(&jsonStr)) {
                ThrowError(isolate, "Web.emit: payload is not JSON-serializable");
                return;
            }
            detail = v8pp::from_v8<std::string>(isolate, jsonStr);
            // JSON.stringify(function/symbol) yields literal "undefined"
            if (detail == "undefined") {
                detail.clear();
            }
        }

        std::string script = "window.dispatchEvent(new CustomEvent('" + EscapeJSString(eventName) + "'";
        if (!detail.empty()) {
            script += ", { detail: " + detail + " }";
        }
        script += "));";

        view->EvaluateScript(script);
        args.GetReturnValue().Set(true);
    }

    void Web::GetScreenSizeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        int width = 0, height = 0;
        if (auto *gui = CoreModules::GetWebManager()) {
            const auto viewport = gui->GetViewportConfiguration();
            width               = viewport.width;
            height              = viewport.height;
        }

        v8::Local<v8::Object> result = v8::Object::New(isolate);
        result->Set(context, v8pp::to_v8(isolate, "width"), v8pp::to_v8(isolate, width)).Check();
        result->Set(context, v8pp::to_v8(isolate, "height"), v8pp::to_v8(isolate, height)).Check();
        args.GetReturnValue().Set(result);
    }

    void Web::AttachViewEvents(Framework::GUI::View *view, int viewId) {
        view->SetOnViewEventCallback([viewId](const Framework::GUI::ViewEventData &data) {
            QueueViewEvent(viewId, data);
        });

        // CreateView is synchronous, so the real Created already fired; replay it.
        if (view->IsCreated()) {
            Framework::GUI::ViewEventData created;
            created.event = Framework::GUI::ViewEvent::Created;
            QueueViewEvent(viewId, created);
        }
    }

    void Web::QueueViewEvent(int viewId, const Framework::GUI::ViewEventData &data) {
        // bounded: a reload storm outruns a stalled scripting tick
        constexpr size_t kMaxPendingViewEvents = 1024;

        std::scoped_lock lock(_mutex);
        if (_pendingViewEvents.size() >= kMaxPendingViewEvents) {
            _pendingViewEvents.pop_front();
        }
        _pendingViewEvents.push_back({viewId, data});
    }

    void Web::Update() {
        auto *manager = _resourceManager;
        if (!manager) {
            return;
        }
        auto *engine = manager->GetJSEngine();
        if (!engine || !engine->IsInitialized()) {
            return;
        }

        std::deque<QueuedViewEvent> pending;
        {
            std::scoped_lock lock(_mutex);
            if (_pendingViewEvents.empty()) {
                return;
            }
            pending.swap(_pendingViewEvents);
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        auto &events = manager->GetEvents();
        for (const auto &queued : pending) {
            DispatchViewEvent(isolate, context, events, queued);
        }
    }

    void Web::DispatchViewEvent(v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::Builtins::Events &events, const QueuedViewEvent &queued) {
        // Owner-scoped; a destroyed view has none, dropping its still-queued events.
        std::string owner;
        {
            std::scoped_lock lock(_mutex);
            const auto it = _viewOwners.find(queued.viewId);
            if (it == _viewOwners.end()) {
                return;
            }
            owner = it->second;
        }

        const std::string eventName(ViewEventName(queued.data.event));
        std::vector<v8::Local<v8::Value>> args {BuildViewEventPayload(isolate, context, queued.viewId, queued.data)};
        (void)events.EmitReservedTo(isolate, context, eventName, args, owner);
    }

    void Web::DispatchPageEvent(int viewId, const std::string &eventName, const std::string &payload) {
        auto *manager = _resourceManager;
        if (!manager) {
            return;
        }
        auto *engine = manager->GetJSEngine();
        if (!engine || !engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        // copy out so handlers can call on/off/destroyView reentrantly
        std::vector<std::pair<v8::Local<v8::Function>, std::string>> handlers;
        {
            std::scoped_lock lock(_mutex);
            auto viewIt = _handlers.find(viewId);
            if (viewIt == _handlers.end()) {
                return;
            }
            auto eventIt = viewIt->second.find(eventName);
            if (eventIt == viewIt->second.end()) {
                return;
            }
            handlers.reserve(eventIt->second.size());
            for (const auto &entry : eventIt->second) {
                handlers.emplace_back(entry.callback.Get(isolate), entry.resourceName);
            }
        }

        // JSON payloads arrive parsed, anything else as the raw string
        v8::Local<v8::Value> arg = v8::Undefined(isolate);
        if (!payload.empty()) {
            arg = v8pp::to_v8(isolate, payload);
            v8::TryCatch parseTry(isolate);
            v8::Local<v8::Value> parsed;
            if (v8::JSON::Parse(context, v8pp::to_v8(isolate, payload)).ToLocal(&parsed)) {
                arg = parsed;
            }
        }

        for (const auto &[fn, resourceName] : handlers) {
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> argv[] = {arg};
            (void)fn->Call(context, context->Global(), 1, argv);
            if (tryCatch.HasCaught()) {
                const std::string error = Framework::Scripting::FormatV8Exception(isolate, tryCatch, "Unknown error in web view event handler");
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Web view {} event '{}' handler error: {}", resourceName, viewId, eventName, error);
            }
        }
    }

    bool Web::DestroyViewInternal(int viewId) {
        {
            std::scoped_lock lock(_mutex);
            if (_viewOwners.erase(viewId) == 0) {
                return false;
            }
            _handlers.erase(viewId);
            std::erase_if(_pendingViewEvents, [viewId](const QueuedViewEvent &queued) {
                return queued.viewId == viewId;
            });
        }
        if (auto *gui = CoreModules::GetWebManager()) {
            gui->DestroyView(viewId);
        }
        return true;
    }

    void Web::CleanupResource(const std::string &resourceName) {
        std::vector<int> viewsToDestroy;
        std::vector<std::pair<int, std::string>> emptiedEvents;
        {
            std::scoped_lock lock(_mutex);
            for (const auto &[id, owner] : _viewOwners) {
                if (owner == resourceName) {
                    viewsToDestroy.push_back(id);
                }
            }
            for (auto &[id, events] : _handlers) {
                if (std::find(viewsToDestroy.begin(), viewsToDestroy.end(), id) != viewsToDestroy.end()) {
                    continue; // erased wholesale below
                }
                for (auto eventIt = events.begin(); eventIt != events.end();) {
                    auto &handlers = eventIt->second;
                    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                       [&](const Handler &h) {
                                           return h.resourceName == resourceName;
                                       }),
                        handlers.end());
                    if (handlers.empty()) {
                        emptiedEvents.emplace_back(id, eventIt->first);
                        eventIt = events.erase(eventIt);
                    }
                    else {
                        ++eventIt;
                    }
                }
            }
        }

        for (const int id : viewsToDestroy) {
            DestroyViewInternal(id);
        }
        for (const auto &[id, eventName] : emptiedEvents) {
            if (auto *view = GetOwnedView(id)) {
                view->RemoveEventListener(eventName);
            }
        }

        if (!viewsToDestroy.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Destroyed {} web view(s) on resource stop", resourceName, viewsToDestroy.size());
        }
    }

    void Web::Shutdown() {
        std::scoped_lock lock(_mutex);
        _handlers.clear();
        _viewOwners.clear();
        _pendingViewEvents.clear();
        _resourceManager = nullptr;
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
