/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "web.h"

#include <scripting/builtins/script_args.h>

#include <core_modules.h>

#include <gui/manager.h>
#include <gui/view.h>

#include <scripting/bind/v8_callable.h>
#include <scripting/builtins/events.h>
#include <scripting/engine.h>
#include <scripting/engine_helpers.h>
#include <scripting/engine_scope.h>
#include <scripting/lua/lua_runtime.h>
#include <scripting/lua/lua_value_walk.h>
#include <scripting/resource/resource.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/script_log.h>
#include <scripting/scripting_catalog.h>
#include <scripting/transferred_value.h>

#include <logging/logger.h>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace Bind = Framework::Scripting::Bind;
    using Framework::Scripting::TransferredValue;
    using Framework::Scripting::Builtins::ToInt32;

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

        // A string argument as text. A Lua string holds any bytes; one that is not UTF-8 is no text and is
        // refused as a value of another type is, so a view is handed nothing a JavaScript string cannot hold.
        bool ReadText(const Bind::Call &call, size_t index, std::string &out) {
            const Bind::Value value = call.Arg(index);
            if (!value.IsString()) {
                return false;
            }
            out = value.AsString();
            return call.GetLanguage() != Framework::Scripting::Language::Lua || Framework::Scripting::LuaValues::IsValidUtf8(out);
        }

        // The id of a view as the builtin always read one: the number wrapped to 32 bits.
        int ViewId(const Bind::Value &value) {
            return ToInt32(value.AsNumber());
        }

        bool ReadViewId(Bind::Call &call, const char *methodName, int &outId) {
            if (call.ArgCount() < 1 || !call.Arg(0).IsNumber()) {
                call.ThrowError(std::string(methodName) + ": first argument must be a view id");
                return false;
            }
            outId = ViewId(call.Arg(0));
            return true;
        }

        // A member of the options of createView. One of another type than its own counts as left out.
        int IntOption(const Bind::Value &options, const char *key, int defaultValue) {
            const Bind::Value value = options.Member(key);
            return value.IsNumber() ? ToInt32(value.AsNumber()) : defaultValue;
        }

        bool BoolOption(const Bind::Value &options, const char *key, bool defaultValue) {
            const Bind::Value value = options.Member(key);
            return value.IsBoolean() ? value.AsBoolean() : defaultValue;
        }

        // A flag that is optional: only a boolean counts, as it always did.
        bool OptionalFlag(const Bind::Value &value, bool defaultValue) {
            return value.IsBoolean() ? value.AsBoolean() : defaultValue;
        }

        TransferredValue Text(std::string text) {
            TransferredValue value;
            value.kind = TransferredValue::Kind::String;
            value.text = std::move(text);
            return value;
        }

        // A page writes the payload: what nests deeper than this is not rebuilt.
        constexpr int kMaxPayloadDepth = 128;

        // A JSON document as the copy a handler's runtime builds its value from. False when it nests too deeply.
        bool FromJson(const nlohmann::ordered_json &json, int depth, TransferredValue &value) {
            if (depth > kMaxPayloadDepth) {
                return false;
            }
            switch (json.type()) {
            case nlohmann::ordered_json::value_t::null: value.kind = TransferredValue::Kind::Null; break;
            case nlohmann::ordered_json::value_t::boolean:
                value.kind    = TransferredValue::Kind::Boolean;
                value.boolean = json.get<bool>();
                break;
            case nlohmann::ordered_json::value_t::number_integer:
            case nlohmann::ordered_json::value_t::number_unsigned:
            case nlohmann::ordered_json::value_t::number_float:
                value.kind   = TransferredValue::Kind::Number;
                value.number = json.get<double>();
                break;
            case nlohmann::ordered_json::value_t::string: value = Text(json.get<std::string>()); break;
            case nlohmann::ordered_json::value_t::array:
                value.kind = TransferredValue::Kind::Array;
                value.items.reserve(json.size());
                for (const auto &element : json) {
                    if (!FromJson(element, depth + 1, value.items.emplace_back())) {
                        return false;
                    }
                }
                break;
            case nlohmann::ordered_json::value_t::object:
                value.kind = TransferredValue::Kind::Object;
                value.properties.reserve(json.size());
                for (const auto &[key, member] : json.items()) {
                    if (!FromJson(member, depth + 1, value.properties.emplace_back(key, TransferredValue {}).second)) {
                        return false;
                    }
                }
                break;
            default: break;
            }
            return true;
        }

        // The payload of a page event as a handler outside the isolate is given it, under the rules the
        // JavaScript handlers have: nothing for an empty payload, the value for JSON, the text itself for
        // anything else.
        TransferredValue PagePayload(const std::string &payload) {
            if (payload.empty()) {
                return {};
            }
            const auto parsed = nlohmann::ordered_json::parse(payload, nullptr, false);
            TransferredValue value;
            if (parsed.is_discarded() || !FromJson(parsed, 0, value)) {
                return Text(payload);
            }
            return value;
        }

        // What reports a handler of a page event that failed. Queued: the manager applies what the manifest
        // of the resource asks for at its next upkeep, outside any script.
        void ReportFailure(Framework::Scripting::ResourceManager *manager, const std::string &resourceName, int viewId, const std::string &eventName, const std::string &error) {
            const std::string message = fmt::format("Web view {} event '{}' handler error: {}", viewId, Framework::Scripting::BlankedLine(eventName), error);
            if (manager != nullptr) {
                manager->ReportHandlerFailure(resourceName, message);
                return;
            }
            Framework::Scripting::LogScriptError(nullptr, resourceName, message);
        }

        // Whether a resource still runs script: it runs, or it failed and was left running, as errorBehavior
        // continue says. The rule of ResourceManager::ProcessHandlerFailures.
        bool StillRuns(const Framework::Scripting::ResourceManager &manager, const std::string &resourceName) {
            const Framework::Scripting::Resource *resource = manager.GetResource(resourceName);
            if (resource == nullptr) {
                return false;
            }
            const auto state = resource->GetState();
            return state == Framework::Scripting::ResourceState::Running || (state == Framework::Scripting::ResourceState::Error && resource->GetManifest().GetMafiaHubConfig().errorBehavior == "continue");
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
            case Framework::GUI::ViewEvent::Created: setString("url", data.url); break;
            case Framework::GUI::ViewEvent::LoadingStart:
                setString("url", data.url);
                set("isMainFrame", v8pp::to_v8(isolate, data.isMainFrame));
                break;
            case Framework::GUI::ViewEvent::DocumentReady: setString("url", data.url); break;
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
            case Framework::GUI::ViewEvent::Tooltip: setString("text", data.tooltip); break;
            case Framework::GUI::ViewEvent::InputFocusChange: set("focused", v8pp::to_v8(isolate, data.focused)); break;
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
    std::shared_ptr<Web::ManagerLink> Web::_managerLink;
    std::shared_ptr<std::vector<std::string>> Web::_failedOwners = std::make_shared<std::vector<std::string>>();

    const Bind::Namespace &Web::Definition() {
        using Bind::Doc;
        using Bind::Side;
        using Bind::TypeOf;
        using Bind::Type::Custom;

        static const auto definition = [] {
            const auto number  = TypeOf<double>();
            const auto boolean = TypeOf<bool>();
            const auto text    = TypeOf<std::string>();
            const auto handler = Custom("WebEventHandler", "fun(payload: any)");
            const auto options = Custom("{ width?: number; height?: number; x?: number; y?: number; zIndex?: number; visible?: boolean; focus?: boolean }", "{ width?: number, height?: number, x?: number, y?: number, zIndex?: number, visible?: boolean, focus?: boolean }");
            const char *viewId = "Owned view identifier.";
            return Bind::Namespace("Web", "Client-only, resource-owned CEF web-view API exposed as the global Web.", Side::Client)
                .Function("createView", &CreateView,
                    Doc("Creates an origin-locked web view owned by the calling resource.")
                        .Param("url", text, "Resource-relative URL or allowed absolute URL loaded into the view.")
                        .OptionalParam("options", options, "Optional initial pixel geometry, stacking, visibility, and focus settings.")
                        .ReturnType(number)
                        .Returns("Numeric view ID used by the remaining Web methods."),
                    Side::Client)
                .Function("destroyView", &DestroyView, Doc("Destroys an owned view and removes all of its script event handlers.").Param("viewId", number, viewId).ReturnType(boolean).Returns("True when a view was destroyed."), Side::Client)
                .Function("showView", &ShowView, Doc("Makes an owned view visible.").Param("viewId", number, viewId).ReturnType(boolean).Returns("True when the view exists and was updated."), Side::Client)
                .Function("hideView", &HideView, Doc("Hides an owned view.").Param("viewId", number, viewId).ReturnType(boolean).Returns("True when the view exists and was updated."), Side::Client)
                .Function("focusView", &FocusView,
                    Doc("Changes input focus for an owned view.")
                        .Param("viewId", number, viewId)
                        .OptionalParam("focused", boolean, "Whether the view captures keyboard and mouse input; defaults to true.")
                        .ReturnType(boolean)
                        .Returns("True when the view exists and focus was updated."),
                    Side::Client)
                .Function("isViewVisible", &IsViewVisible, Doc("Checks whether an owned view is currently visible.").Param("viewId", number, viewId).ReturnType(boolean).Returns("False for missing or unowned views."), Side::Client)
                .Function("setViewOffscreen", &SetViewOffscreen,
                    Doc("Keeps a hidden view painting so something else can sample it, such as a render target drawing the page onto world geometry. A view that is merely hidden stops painting and the sampled picture freezes.")
                        .Param("viewId", number, viewId)
                        .OptionalParam("offscreen", boolean, "Whether the page keeps painting without being drawn on screen; defaults to true.")
                        .ReturnType(boolean)
                        .Returns("True when the view exists and was updated."),
                    Side::Client)
                .Function("isViewOffscreen", &IsViewOffscreen, Doc("Checks whether an owned view keeps painting while hidden.").Param("viewId", number, viewId).ReturnType(boolean).Returns("False for missing or unowned views."), Side::Client)
                .Function("loadURL", &LoadURL,
                    Doc("Navigates a view and replaces its allowed origin with the new URL's origin.").Param("viewId", number, viewId).Param("url", text, "New resource-relative or allowed absolute URL.").ReturnType(boolean).Returns("True when navigation was requested."),
                    Side::Client)
                .Function("resizeView", &ResizeView,
                    Doc("Resizes an owned view's viewport.")
                        .Param("viewId", number, viewId)
                        .Param("width", number, "New viewport width in pixels.")
                        .Param("height", number, "New viewport height in pixels.")
                        .ReturnType(boolean)
                        .Returns("True when the view exists and was resized."),
                    Side::Client)
                .Function("setViewPosition", &SetViewPosition,
                    Doc("Moves an owned view on screen.")
                        .Param("viewId", number, viewId)
                        .Param("x", number, "New horizontal screen position in pixels.")
                        .Param("y", number, "New vertical screen position in pixels.")
                        .ReturnType(boolean)
                        .Returns("True when the view exists and was moved."),
                    Side::Client)
                .Function("on", &On,
                    Doc("Registers a handler for an event emitted by an owned same-origin page.").Param("viewId", number, viewId).Param("eventName", text, "Page-to-script event name.").Param("handler", handler, "Resource-owned callback invoked by the view's callEvent bridge."),
                    Side::Client)
                .Function("off", &Off,
                    Doc("Removes page-event handlers from an owned view.")
                        .Param("viewId", number, viewId)
                        .Param("eventName", text, "Page-to-script event name.")
                        .OptionalParam("handler", handler, "Optional exact callback; omitting it removes every matching handler owned by the resource.")
                        .ReturnType(boolean)
                        .Returns("True when at least one handler was removed."),
                    Side::Client)
                .Function("emit", &Emit,
                    Doc("Dispatches a CustomEvent into an owned view.")
                        .Param("viewId", number, viewId)
                        .Param("eventName", text, "CustomEvent name dispatched in the page.")
                        .OptionalParam("payload", Custom("unknown", "any"), "Optional JSON-serializable event detail.")
                        .ReturnType(boolean)
                        .Returns("True when the dispatch script was queued."),
                    Side::Client)
                .Function("getScreenSize", &GetScreenSize, Doc("Returns the current client viewport size.").ReturnType(Custom("{ width: number; height: number }", "{ width: integer, height: integer }")).Returns("Width and height in physical pixels."), Side::Client);
        }();
        return definition;
    }

    void Web::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        {
            std::scoped_lock lock(_mutex);
            _handlers.clear();
            _viewOwners.clear();
            _pendingViewEvents.clear();
        }
        // What a handler of the manager before this one still reports is no longer anybody's.
        if (_managerLink != nullptr) {
            _managerLink->manager = nullptr;
        }
        ForgetFailedOwners();
        _resourceManager      = resourceManager;
        _managerLink          = std::make_shared<ManagerLink>();
        _managerLink->manager = resourceManager;

        // So the calls of this isolate name their resource and its manager.
        Bind::SetCallResourceManager(isolate, resourceManager);
        Definition().MaterialiseV8(isolate, context, target, Framework::Scripting::GetScriptingCatalog(isolate), Bind::Side::Client);

        RegisterViewEventMetadata(isolate);
    }

    void Web::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
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

        describe(Framework::GUI::ViewEvent::Created, "BrowserCreatedEvent", "A web view's browser finished being created.", {{"url", "string", "URL the view was created with."}}, "Dispatched once per view, before any navigation, to the resource that created it.");
        describe(Framework::GUI::ViewEvent::LoadingStart, "BrowserLoadingStartEvent", "A frame inside a web view started loading.", {{"url", "string", "URL being loaded."}, {"isMainFrame", "boolean", "False for sub-frame loads."}},
            "Dispatched when any frame of an owned view begins loading.");
        describe(Framework::GUI::ViewEvent::DocumentReady, "BrowserDocumentReadyEvent", "A web view's main frame finished loading.", {{"url", "string", "URL that finished loading."}},
            "Dispatched when an owned view's main document is ready; the earliest point at which the page can receive Web.emit.");
        describe(Framework::GUI::ViewEvent::LoadingFailed, "BrowserLoadingFailedEvent", "A load inside a web view was aborted.",
            {{"url", "string", "URL that failed to load."}, {"description", "string", "CEF error text, such as ERR_CONNECTION_REFUSED."}, {"errorCode", "number", "CEF error code."}, {"isMainFrame", "boolean", "False for sub-frame failures."}},
            "Dispatched when a frame of an owned view fails to load.");
        describe(Framework::GUI::ViewEvent::Navigate, "BrowserNavigateEvent", "A web view was asked to navigate.", {{"url", "string", "Requested URL."}, {"isMainFrame", "boolean", "False for sub-frame navigation."}, {"blocked", "boolean", "Whether the request was refused."}},
            "Dispatched for every navigation an owned view is asked to perform, refused or not.");
        describe(Framework::GUI::ViewEvent::Popup, "BrowserPopupEvent", "A page tried to open a new window or tab.", {{"url", "string", "Target URL of the blocked popup."}, {"openerUrl", "string", "URL of the frame that requested it."}},
            "Dispatched when an owned view blocks a popup; windowless views cannot host one, so handle the URL yourself.");
        describe(Framework::GUI::ViewEvent::CursorChange, "BrowserCursorChangeEvent", "The cursor shape a page is asking for.", {{"cursor", "string", "CSS-style cursor name, or \"custom\" for shapes without one."}, {"cursorType", "number", "Raw CEF cursor type."}},
            "Dispatched when an owned view's requested cursor shape changes.");
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

    Framework::Scripting::ResourceManager *Web::ManagerOf(const Bind::Call &call) {
        auto *manager = call.GetResourceManager();
        return manager != nullptr && manager == _resourceManager ? manager : nullptr;
    }

    Framework::GUI::View *Web::GetCallerView(const Bind::Call &call, int viewId) {
        if (!ManagerOf(call)) {
            return nullptr;
        }
        const std::string caller = call.ResourceName();
        {
            std::scoped_lock lock(_mutex);
            const auto it = _viewOwners.find(viewId);
            if (it == _viewOwners.end() || it->second != caller) {
                return nullptr;
            }
        }
        auto *gui = CoreModules::GetWebManager();
        return gui ? gui->GetView(viewId) : nullptr;
    }

    void Web::CreateView(Bind::Call &call) {
        std::string url;
        if (call.ArgCount() < 1 || !ReadText(call, 0, url)) {
            call.ThrowError("Web.createView requires a url string");
            return;
        }

        if (!ManagerOf(call)) {
            call.ThrowError("Web.createView: resource manager not available");
            return;
        }

        const std::string owner = call.ResourceName();
        if (owner.empty()) {
            call.ThrowError("Web.createView: must be called from within a resource");
            return;
        }

        auto *gui = CoreModules::GetWebManager();
        if (!gui || !gui->IsInitialized()) {
            call.ThrowError("Web.createView: web view manager is not available");
            return;
        }

        // Read off an object or a table; anything else has no member and leaves the defaults.
        const Bind::Value options = call.Arg(1);
        const int width           = IntOption(options, "width", 0);
        const int height          = IntOption(options, "height", 0);
        const int x               = IntOption(options, "x", 0);
        const int y               = IntOption(options, "y", 0);
        const int zIndex          = IntOption(options, "zIndex", 0);
        const bool visible        = BoolOption(options, "visible", true);
        const bool focus          = BoolOption(options, "focus", false);
        // What a getter of the script threw is the result of the call: no view is made for it to lose.
        if (call.HasThrown()) {
            return;
        }

        const int id = gui->CreateView(url, width, height, x, y);
        if (id < 0) {
            call.ThrowError("Web.createView: failed to create web view");
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

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Created web view {} ({})", owner, id, Framework::Scripting::BlankedLine(url));
        call.Return(id);
    }

    void Web::DestroyView(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.destroyView", id)) {
            return;
        }

        call.Return(GetCallerView(call, id) != nullptr && DestroyViewInternal(id));
    }

    void Web::ShowView(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.showView", id)) {
            return;
        }

        auto *view = GetCallerView(call, id);
        if (!view) {
            call.Return(false);
            return;
        }
        view->Display(true);
        call.Return(true);
    }

    void Web::HideView(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.hideView", id)) {
            return;
        }

        auto *view = GetCallerView(call, id);
        if (!view) {
            call.Return(false);
            return;
        }
        // drop focus too, else the mod's focus-based input gate stays on
        view->Display(false);
        view->Focus(false);
        call.Return(true);
    }

    void Web::FocusView(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.focusView", id)) {
            return;
        }

        const bool focused = OptionalFlag(call.Arg(1), true);

        auto *view = GetCallerView(call, id);
        if (!view) {
            call.Return(false);
            return;
        }
        view->Focus(focused);
        call.Return(true);
    }

    void Web::IsViewVisible(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.isViewVisible", id)) {
            return;
        }

        auto *view = GetCallerView(call, id);
        call.Return(view != nullptr && view->ShouldDisplay());
    }

    void Web::SetViewOffscreen(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.setViewOffscreen", id)) {
            return;
        }

        const bool offscreen = OptionalFlag(call.Arg(1), true);

        auto *view = GetCallerView(call, id);
        if (!view) {
            call.Return(false);
            return;
        }
        view->SetOffscreen(offscreen);
        call.Return(true);
    }

    void Web::IsViewOffscreen(Bind::Call &call) {
        int id = -1;
        if (!ReadViewId(call, "Web.isViewOffscreen", id)) {
            return;
        }

        auto *view = GetCallerView(call, id);
        call.Return(view != nullptr && view->IsOffscreen());
    }

    void Web::LoadURL(Bind::Call &call) {
        std::string url;
        if (call.ArgCount() < 2 || !call.Arg(0).IsNumber() || !ReadText(call, 1, url)) {
            call.ThrowError("Web.loadURL requires 2 arguments: viewId, url");
            return;
        }

        auto *view = GetCallerView(call, ViewId(call.Arg(0)));
        if (!view) {
            call.Return(false);
            return;
        }
        view->LoadURL(url);
        call.Return(true);
    }

    void Web::ResizeView(Bind::Call &call) {
        if (call.ArgCount() < 3 || !call.Arg(0).IsNumber() || !call.Arg(1).IsNumber() || !call.Arg(2).IsNumber()) {
            call.ThrowError("Web.resizeView requires 3 arguments: viewId, width, height");
            return;
        }

        const int width  = ToInt32(call.Arg(1).AsNumber());
        const int height = ToInt32(call.Arg(2).AsNumber());

        auto *view = GetCallerView(call, ViewId(call.Arg(0)));
        if (!view || width <= 0 || height <= 0) {
            call.Return(false);
            return;
        }
        // explicit size means the view no longer tracks the viewport
        view->SetAutoResize(false);
        view->Resize(width, height);
        call.Return(true);
    }

    void Web::SetViewPosition(Bind::Call &call) {
        if (call.ArgCount() < 3 || !call.Arg(0).IsNumber() || !call.Arg(1).IsNumber() || !call.Arg(2).IsNumber()) {
            call.ThrowError("Web.setViewPosition requires 3 arguments: viewId, x, y");
            return;
        }

        const int x = ToInt32(call.Arg(1).AsNumber());
        const int y = ToInt32(call.Arg(2).AsNumber());

        auto *view = GetCallerView(call, ViewId(call.Arg(0)));
        if (!view) {
            call.Return(false);
            return;
        }
        view->SetPosition(x, y);
        call.Return(true);
    }

    void Web::On(Bind::Call &call) {
        std::string eventName;
        if (call.ArgCount() < 3 || !call.Arg(0).IsNumber() || !ReadText(call, 1, eventName) || !call.Arg(2).IsFunction()) {
            call.ThrowError("Web.on requires 3 arguments: viewId, eventName, handler");
            return;
        }

        if (!ManagerOf(call)) {
            call.ThrowError("Web.on: resource manager not available");
            return;
        }

        const int id = ViewId(call.Arg(0));

        if (eventName.empty()) {
            call.ThrowError("Web.on: eventName must not be empty");
            return;
        }

        // The resource the handler belongs to, before the one that is calling.
        const std::string resourceName = call.OwnerOf(call.Arg(2));
        if (resourceName.empty()) {
            call.ThrowError("Web.on: must be called from within a resource");
            return;
        }

        auto *view = GetCallerView(call, id);
        if (!view) {
            call.ThrowError("Web.on: unknown web view " + std::to_string(id));
            return;
        }

        std::shared_ptr<Bind::ScriptCallable> callback = call.Arg(2).ToCallable();
        if (callback == nullptr) {
            call.ThrowError("Web.on requires 3 arguments: viewId, eventName, handler");
            return;
        }

        bool firstForEvent = false;
        {
            std::scoped_lock lock(_mutex);
            auto &handlers = _handlers[id][eventName];
            firstForEvent  = handlers.empty();

            Handler entry;
            entry.callback     = std::move(callback);
            entry.resourceName = resourceName;
            handlers.push_back(std::move(entry));
        }

        if (firstForEvent) {
            view->AddEventListener(eventName, [id, eventName](const std::string &payload) {
                DispatchPageEvent(id, eventName, payload);
            });
        }
    }

    void Web::Off(Bind::Call &call) {
        std::string eventName;
        if (call.ArgCount() < 2 || !call.Arg(0).IsNumber() || !ReadText(call, 1, eventName)) {
            call.ThrowError("Web.off requires at least 2 arguments: viewId, eventName");
            return;
        }

        const int id = ViewId(call.Arg(0));

        // The same function gives the same callable, so the handler is found again by it. Anything that is
        // not a function is no filter.
        const std::shared_ptr<Bind::ScriptCallable> handler = call.Arg(2).ToCallable();

        if (!GetCallerView(call, id)) {
            call.Return(false);
            return;
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
                    if (handler == nullptr) {
                        removed = !handlers.empty();
                        handlers.clear();
                    }
                    else {
                        const auto before = handlers.size();
                        std::erase_if(handlers, [&](const Handler &h) {
                            return h.callback->Equals(*handler);
                        });
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

        call.Return(removed);
    }

    void Web::Emit(Bind::Call &call) {
        std::string eventName;
        if (call.ArgCount() < 2 || !call.Arg(0).IsNumber() || !ReadText(call, 1, eventName)) {
            call.ThrowError("Web.emit requires at least 2 arguments: viewId, eventName");
            return;
        }

        auto *view = GetCallerView(call, ViewId(call.Arg(0)));
        if (!view) {
            call.Return(false);
            return;
        }

        // The page gets JSON text and nothing else. A payload left out, undefined or nil is no detail; the
        // null of JavaScript is a detail of null. Neither is a function, which JSON has no form for in
        // either language, nor what JSON.stringify leaves out.
        std::string detail;
        const Bind::Value payload = call.Arg(2);
        if ((!payload.IsNil() || payload.IsNull()) && !payload.IsFunction()) {
            bool omitted = false;
            auto json    = payload.ToJson(omitted);
            if (json) {
                detail = std::move(json).GetValue();
            }
            else if (!omitted) {
                call.ThrowError("Web.emit: payload is not JSON-serializable");
                return;
            }
        }

        std::string script = "window.dispatchEvent(new CustomEvent('" + EscapeJSString(eventName) + "'";
        if (!detail.empty()) {
            script += ", { detail: " + detail + " }";
        }
        script += "));";

        view->EvaluateScript(script);
        call.Return(true);
    }

    void Web::GetScreenSize(Bind::Call &call) {
        int width = 0, height = 0;
        if (auto *gui = CoreModules::GetWebManager()) {
            const auto viewport = gui->GetViewportConfiguration();
            width               = viewport.width;
            height              = viewport.height;
        }

        Bind::Value result = call.NewObject();
        result.Set("width", call.Integer(width));
        result.Set("height", call.Integer(height));
        call.Return(result);
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

    void Web::ForgetFailedOwners() {
        if (!_failedOwners->empty()) {
            _failedOwners = std::make_shared<std::vector<std::string>>();
        }
    }

    void Web::Update() {
        ForgetFailedOwners();

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

        Framework::Scripting::EngineScope scope(engine);
        v8::Isolate *isolate           = scope.Isolate();
        v8::Local<v8::Context> context = scope.Context();

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

    void Web::DispatchPageEvent(int viewId, std::string eventName, std::string payload) {
        auto *manager = _resourceManager;
        if (!manager) {
            return;
        }

        // copy out so handlers can call on/off/destroyView reentrantly
        std::vector<Handler> handlers;
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
            handlers = eventIt->second;
        }
        // Held: a handler may shut the scripting down.
        const std::shared_ptr<ManagerLink> link                = _managerLink;
        const std::shared_ptr<std::vector<std::string>> failed = _failedOwners;

        // The JavaScript handlers of the client's isolate are called in it, one after the other with the one
        // value the payload parses to, as they always were: a handler sees what the one before it wrote in
        // it. Any other handler, a Lua one, is invoked in its runtime with a copy.
        auto *engine               = manager->GetJSEngine();
        v8::Isolate *engineIsolate = engine && engine->IsInitialized() ? engine->GetIsolate() : nullptr;
        std::optional<Framework::Scripting::EngineScope> scope;
        v8::Local<v8::Value> arg;
        std::optional<std::vector<TransferredValue>> copied;

        for (const Handler &handler : handlers) {
            // A handler that ran before it may have shut the scripting down: nobody is left to call.
            if (link == nullptr || link->manager == nullptr) {
                break;
            }
            // Or stopped its resource, in either language, or failed: the resource then waits for the next
            // update.
            if (!handler.callback->IsAlive() || !StillRuns(*link->manager, handler.resourceName) || std::find(failed->begin(), failed->end(), handler.resourceName) != failed->end()) {
                continue;
            }

            if (engineIsolate != nullptr && handler.callback->GetLanguage() == Framework::Scripting::Language::JavaScript) {
                const auto &callable = static_cast<const Bind::V8Callable &>(*handler.callback);
                if (callable.Isolate() == engineIsolate) {
                    if (!scope) {
                        scope.emplace(engine);
                        // JSON payloads arrive parsed, anything else as the raw string
                        arg = v8::Undefined(engineIsolate);
                        if (!payload.empty()) {
                            arg = v8pp::to_v8(engineIsolate, payload);
                            v8::TryCatch parseTry(engineIsolate);
                            v8::Local<v8::Value> parsed;
                            if (v8::JSON::Parse(scope->Context(), v8pp::to_v8(engineIsolate, payload)).ToLocal(&parsed)) {
                                arg = parsed;
                            }
                        }
                    }
                    v8::Local<v8::Context> context = scope->Context();
                    v8::Local<v8::Function> fn     = callable.Function();
                    if (fn.IsEmpty()) {
                        continue;
                    }
                    v8::TryCatch tryCatch(engineIsolate);
                    v8::Local<v8::Value> argv[] = {arg};
                    (void)fn->Call(context, context->Global(), 1, argv);
                    if (tryCatch.HasCaught()) {
                        const std::string error = Framework::Scripting::FormatV8Exception(engineIsolate, tryCatch, "Unknown error in web view event handler");
                        failed->push_back(handler.resourceName);
                        ReportFailure(link->manager, handler.resourceName, viewId, eventName, error);
                    }
                    continue;
                }
            }

            if (!copied) {
                copied.emplace();
                copied->push_back(PagePayload(payload));
            }
            // Reported when it raised and when what it waited on failed later; one cut short because its
            // runtime stopped has nothing to report.
            handler.callback->Invoke(*copied, Bind::ReturnMode::Outcome, [link, failed, resourceName = handler.resourceName, viewId, eventName](Bind::CallOutcome &&outcome) {
                if (outcome.threw && outcome.error != Bind::kRuntimeStopped) {
                    failed->push_back(resourceName);
                    ReportFailure(link->manager, resourceName, viewId, eventName, outcome.error);
                }
            });
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
                if (std::ranges::contains(viewsToDestroy, id)) {
                    continue; // erased wholesale below
                }
                for (auto eventIt = events.begin(); eventIt != events.end();) {
                    auto &handlers = eventIt->second;
                    std::erase_if(handlers, [&](const Handler &h) {
                        return h.resourceName == resourceName;
                    });
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
        ForgetFailedOwners();
        _resourceManager = nullptr;
        // A handler that is still running reports to nobody.
        if (_managerLink != nullptr) {
            _managerLink->manager = nullptr;
            _managerLink.reset();
        }
    }

    size_t Web::ViewCount() {
        std::scoped_lock lock(_mutex);
        return _viewOwners.size();
    }

    size_t Web::PendingEventCount() {
        std::scoped_lock lock(_mutex);
        return _pendingViewEvents.size();
    }

    size_t Web::HandlerCount() {
        std::scoped_lock lock(_mutex);
        size_t count = 0;
        for (const auto &[id, events] : _handlers) {
            for (const auto &[eventName, handlers] : events) {
                count += handlers.size();
            }
        }
        return count;
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
