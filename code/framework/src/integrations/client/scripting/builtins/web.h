/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <gui/view_events.h>

#include <v8.h>

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {
    class Events;
} // namespace Framework::Scripting::Builtins

namespace Framework::GUI {
    class View;
} // namespace Framework::GUI

namespace Framework::Integrations::Client::Scripting::Builtins {

    // Client-side web view API, exposed as the global Web:
    //   createView(url, {width, height, x, y, zIndex, visible, focus}?) -> id
    //   destroyView / showView / hideView / focusView / isViewVisible
    //   loadURL(id, url) / resizeView(id, w, h) / setViewPosition(id, x, y)
    //   on(id, event, handler) / off(id, event, handler?)  page -> script via callEvent()
    //   emit(id, event, payload?)                          script -> page as CustomEvent
    //   getScreenSize() -> {width, height}  client viewport, in pixels
    // Views are resource-owned: destroyed when the creating resource stops, and only
    // views created here are reachable from scripts. Views are origin-locked to their
    // URL (loadURL re-locks): cross-origin main-frame navigation is blocked and
    // callEvent() from foreign origins is dropped.
    //
    // Browser-reported events go to the owning resource as reserved "browser*" Events instead
    // of through on(), which carries page-authored names a page could otherwise forge.
    // See docs/scripting_web_events.md.
    class Web final {
      public:
        static void Register(v8::Isolate *isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> target,
                             Framework::Scripting::ResourceManager *resourceManager);

        // Drains queued browser events into script. Once per scripting tick.
        static void Update();

        static void CleanupResource(const std::string &resourceName);

        // Reserved browser events have no Web method to hang catalog docs off.
        static void RegisterViewEventMetadata(v8::Isolate *isolate);

        // Must run before the isolate is disposed.
        static void Shutdown();

      private:
        struct Handler {
            v8::Global<v8::Function> callback;
            std::string resourceName;
        };

        struct QueuedViewEvent {
            int viewId;
            Framework::GUI::ViewEventData data;
        };

        static void CreateViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void DestroyViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ShowViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void HideViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void FocusViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsViewVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void LoadURLCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ResizeViewCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetViewPositionCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetScreenSizeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static bool DestroyViewInternal(int viewId);
        static void DispatchPageEvent(int viewId, const std::string &eventName, const std::string &payload);
        static Framework::GUI::View *GetOwnedView(int viewId);

        static void AttachViewEvents(Framework::GUI::View *view, int viewId);
        static void QueueViewEvent(int viewId, const Framework::GUI::ViewEventData &data);
        static void DispatchViewEvent(v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::Builtins::Events &events, const QueuedViewEvent &queued);

        // viewId -> eventName -> handlers, for page events raised through callEvent()
        static std::map<int, std::map<std::string, std::vector<Handler>>> _handlers;
        // viewId -> owning resource
        static std::map<int, std::string> _viewOwners;
        // Queued so script never runs while CEF is mid-callback; drained by Update.
        static std::deque<QueuedViewEvent> _pendingViewEvents;
        static std::mutex _mutex;

        static Framework::Scripting::ResourceManager *_resourceManager;
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
