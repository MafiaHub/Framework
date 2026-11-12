/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/bind/callable.h>
#include <scripting/bind/namespace.h>

#include <v8.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class LuaRuntime;
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Integrations::Client::Scripting::Builtins {

    // Client-side keybinding API, exposed as the global Key to JavaScript and Lua. See
    // docs/scripting_keybindings.md. Polled once per frame via Update(); the host mod supplies the
    // input gate through SetActiveCallback so binds stay quiet while UI owns input.
    class Keybinds final {
      public:
        // Key, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        // Starts from no binding: the ones of a context that was reset are gone with it.
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

        // Publishes Key as a global of a Lua runtime. The bindings are one table for both languages.
        static void RegisterLua(Framework::Scripting::LuaRuntime &runtime);

        // Poll physical keys, detect edges, dispatch matching handlers into their scripts.
        static void Update();

        // Predicate returning whether binds may fire this frame (input not captured).
        static void SetActiveCallback(std::function<bool()> callback);

        // Drops the bindings of a resource, in either language. With a manager, only those made in its
        // runtimes.
        static void CleanupResource(const std::string &resourceName);
        static void CleanupResource(const Framework::Scripting::ResourceManager *manager, const std::string &resourceName);

        // Must run before the isolate is disposed, and before the resource manager goes.
        static void Shutdown();

        // How many handlers are bound, over every key and both languages.
        static size_t HandlerCount();

      private:
        enum class State : uint8_t {
            Down,
            Up,
            Both
        };

        // The manager a failed handler is reported to. Shared with the calls still running when
        // Shutdown clears it, which then report to nobody.
        struct ManagerLink {
            Framework::Scripting::ResourceManager *manager = nullptr;
        };

        struct Handler {
            std::shared_ptr<Framework::Scripting::Bind::ScriptCallable> callback;
            std::string resourceName;
            std::shared_ptr<ManagerLink> manager;
            State state;
            uint32_t boundNotch = 0; // wheel notches up to this one predate the bind
        };

        struct Bucket {
            int vk        = 0;
            bool prevDown = false;
            std::vector<Handler> handlers;
        };

        static void BindKey(Framework::Scripting::Bind::Call &call);
        static void UnbindKey(Framework::Scripting::Bind::Call &call);
        static void IsDown(Framework::Scripting::Bind::Call &call);
        static void GetLabel(Framework::Scripting::Bind::Call &call);

        static bool ParseState(const std::string &s, State &out);
        static bool GateAllowed();
        static std::shared_ptr<ManagerLink> LinkOf(Framework::Scripting::ResourceManager *manager);

        // canonical (lowercased) key name -> its poll bucket
        static std::map<std::string, Bucket> _buckets;
        static std::mutex _mutex;

        // Update() only.
        static bool _sourceWasUnavailable;
        static uint32_t _wheelCursor; // last wheel notch read
        // While dispatching, the last notch handed out, so a handler bound mid-tick gets the rest.
        static std::optional<uint32_t> _dispatchNotch;

        static std::function<bool()> _activeCallback;

        // The link of each manager a handler was bound under, until Shutdown. Guarded by _mutex.
        static std::map<const Framework::Scripting::ResourceManager *, std::shared_ptr<ManagerLink>> _managers;
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
