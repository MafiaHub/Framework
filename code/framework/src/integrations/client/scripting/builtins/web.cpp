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

#include <scripting/engine.h>
#include <scripting/engine_helpers.h>
#include <scripting/resource/resource.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>

#include <logging/logger.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
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
    } // anonymous namespace

    std::map<int, std::map<std::string, std::vector<Web::Handler>>> Web::_handlers;
    std::map<int, std::string> Web::_viewOwners;
    std::mutex Web::_mutex;
    Framework::Scripting::ResourceManager *Web::_resourceManager = nullptr;

    void Web::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> frameworkObj, Framework::Scripting::ResourceManager *resourceManager) {
        {
            std::scoped_lock lock(_mutex);
            _handlers.clear();
            _viewOwners.clear();
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

        frameworkObj->Set(context, v8pp::to_v8(isolate, "Web"), webObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Web", "Client-only, resource-owned CEF web-view API exposed as Framework.Web.");
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

        if (auto *view = gui->GetView(id)) {
            view->SetGarbageCollected(true);
            view->LockToOrigin(url);
            view->SetZIndex(zIndex);
            view->Display(visible);
            if (focus) {
                view->Focus(true);
            }
        }

        {
            std::scoped_lock lock(_mutex);
            _viewOwners[id] = owner;
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
                DispatchViewEvent(id, eventName, payload);
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

    void Web::DispatchViewEvent(int viewId, const std::string &eventName, const std::string &payload) {
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
        _resourceManager = nullptr;
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
