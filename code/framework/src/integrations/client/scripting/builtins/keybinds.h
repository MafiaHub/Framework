/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Integrations::Client::Scripting::Builtins {

    // Client-side keybinding API, exposed as Framework.Key. See docs/scripting_keybindings.md.
    // Polled once per frame via Update(); the host mod supplies the input gate through
    // SetActiveCallback so binds stay quiet while UI owns input.
    class Keybinds final {
      public:
        static void Register(v8::Isolate *isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> frameworkObj,
                             Framework::Scripting::ResourceManager *resourceManager);

        // Poll physical keys, detect edges, dispatch matching handlers into JS.
        static void Update();

        // Predicate returning whether binds may fire this frame (input not captured).
        static void SetActiveCallback(std::function<bool()> callback);

        static void CleanupResource(const std::string &resourceName);

        // Must run before the isolate is disposed.
        static void Shutdown();

      private:
        enum class State : uint8_t { Down, Up, Both };

        struct Handler {
            v8::Global<v8::Function> callback;
            std::string resourceName;
            State state;
        };

        struct Bucket {
            int vk = 0;
            bool prevDown = false;
            std::vector<Handler> handlers;
        };

        static void BindCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void UnbindCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsDownCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static bool ParseState(const std::string &s, State &out);
        static bool GateAllowed();

        // canonical (lowercased) key name -> its poll bucket
        static std::map<std::string, Bucket> _buckets;
        static std::mutex _mutex;
        static std::function<bool()> _activeCallback;

        static Framework::Scripting::ResourceManager *_resourceManager;
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
