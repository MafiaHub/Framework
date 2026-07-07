/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::GUI {
    class View;
} // namespace Framework::GUI

namespace Framework::Integrations::Client::Scripting::Builtins {

    // Client-side web view API, exposed as Framework.Web:
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
    class Web final {
      public:
        static void Register(v8::Isolate *isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> frameworkObj,
                             Framework::Scripting::ResourceManager *resourceManager);

        static void CleanupResource(const std::string &resourceName);

        // Must run before the isolate is disposed.
        static void Shutdown();

      private:
        struct Handler {
            v8::Global<v8::Function> callback;
            std::string resourceName;
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
        static void DispatchViewEvent(int viewId, const std::string &eventName, const std::string &payload);
        static Framework::GUI::View *GetOwnedView(int viewId);

        // viewId -> eventName -> handlers
        static std::map<int, std::map<std::string, std::vector<Handler>>> _handlers;
        // viewId -> owning resource
        static std::map<int, std::string> _viewOwners;
        static std::mutex _mutex;

        static Framework::Scripting::ResourceManager *_resourceManager;
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
