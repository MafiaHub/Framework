/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <gui/view_events.h>

#include <scripting/bind/callable.h>
#include <scripting/bind/namespace.h>
#include <scripting/bind/registrar.h>

#include <v8.h>

#include <deque>
#include <map>
#include <memory>
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

    // Client-side web view API, exposed as the global Web to JavaScript and Lua:
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
    //
    // The views, their owners and their handlers are one table for both languages, and belong to the
    // resource manager Register was given: a call of a runtime of another manager finds none.
    class Web final {
      public:
        // Web, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        // Publishes Web as a global in the languages of `languages`, in the runtimes of a client.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All, Framework::Scripting::ResourceManager *resourceManager = nullptr);

        // Drains queued browser events into script. Once per scripting tick.
        static void Update();

        // Destroys the views of a resource and drops its handlers, in either language.
        static void CleanupResource(const std::string &resourceName);

        // Reserved browser events have no Web method to hang catalog docs off.
        static void RegisterViewEventMetadata(v8::Isolate *isolate);

        // Must run before the isolate is disposed, and before the resource manager goes.
        static void Shutdown();

        // Diagnostics and tests: the views scripts own, the handlers they keep on them.
        static size_t ViewCount();
        static size_t HandlerCount();
        // And the browser events queued for the next Update.
        static size_t PendingEventCount();

      private:
        // The manager a failed handler is reported to. Shared with the calls still running when Shutdown
        // clears it, which then report to nobody.
        struct ManagerLink {
            Framework::Scripting::ResourceManager *manager = nullptr;
        };

        struct Handler {
            std::shared_ptr<Framework::Scripting::Bind::ScriptCallable> callback;
            std::string resourceName;
        };

        struct QueuedViewEvent {
            int viewId;
            Framework::GUI::ViewEventData data;
        };

        static void CreateView(Framework::Scripting::Bind::Call &call);
        static void DestroyView(Framework::Scripting::Bind::Call &call);
        static void ShowView(Framework::Scripting::Bind::Call &call);
        static void HideView(Framework::Scripting::Bind::Call &call);
        static void FocusView(Framework::Scripting::Bind::Call &call);
        static void IsViewVisible(Framework::Scripting::Bind::Call &call);
        static void SetViewOffscreen(Framework::Scripting::Bind::Call &call);
        static void IsViewOffscreen(Framework::Scripting::Bind::Call &call);
        static void LoadURL(Framework::Scripting::Bind::Call &call);
        static void ResizeView(Framework::Scripting::Bind::Call &call);
        static void SetViewPosition(Framework::Scripting::Bind::Call &call);
        static void On(Framework::Scripting::Bind::Call &call);
        static void Off(Framework::Scripting::Bind::Call &call);
        static void Emit(Framework::Scripting::Bind::Call &call);
        static void GetScreenSize(Framework::Scripting::Bind::Call &call);

        static bool DestroyViewInternal(int viewId);
        static void DispatchPageEvent(int viewId, std::string eventName, std::string payload);
        static Framework::GUI::View *GetOwnedView(int viewId);
        // The view, if the calling resource is the one that created it. Script-facing calls use this;
        // GetOwnedView is for teardown, which acts whoever the owner was.
        static Framework::GUI::View *GetCallerView(const Framework::Scripting::Bind::Call &call, int viewId);
        // The manager the views belong to, when the call comes from one of its runtimes. Null otherwise.
        static Framework::Scripting::ResourceManager *ManagerOf(const Framework::Scripting::Bind::Call &call);

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
        // Of _resourceManager, from Register until Shutdown.
        static std::shared_ptr<ManagerLink> _managerLink;

        // The resources a page-event handler of which failed since the last Update. Their other handlers wait
        // for the next one: a resource that runs every handler into its deadline costs a frame one deadline,
        // however many handlers it has and however many events its page sends. Shared with the calls that end
        // later, which add to it in vain.
        static std::shared_ptr<std::vector<std::string>> _failedOwners;
        static void ForgetFailedOwners();
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
