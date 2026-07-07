/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// safe_win32 first: pulls in WinSock2 before Windows.h to avoid winsock1 conflicts.
#include <utils/safe_win32.h>

#include "keybinds.h"

#include <scripting/engine.h>
#include <scripting/engine_helpers.h>
#include <scripting/resource/resource_manager.h>

#include <logging/logger.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        // Handler origin, explicit context, then V8 stack — same order as Web/Events.
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

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        // Fixed name -> Win32 virtual-key table. Case-insensitive; several friendly aliases.
        const std::unordered_map<std::string, int> &KeyTable() {
            static const std::unordered_map<std::string, int> table = [] {
                std::unordered_map<std::string, int> t;
                for (char c = 'a'; c <= 'z'; ++c) {
                    t[std::string(1, c)] = 'A' + (c - 'a');
                }
                for (char c = '0'; c <= '9'; ++c) {
                    t[std::string(1, c)] = c;
                }
                for (int i = 1; i <= 12; ++i) {
                    t["f" + std::to_string(i)] = VK_F1 + (i - 1);
                }
                for (int i = 0; i <= 9; ++i) {
                    const int vk        = VK_NUMPAD0 + i;
                    t["numpad" + std::to_string(i)] = vk;
                    t["num" + std::to_string(i)]    = vk;
                }
                t["space"]     = VK_SPACE;
                t["enter"]     = VK_RETURN;
                t["return"]    = VK_RETURN;
                t["escape"]    = VK_ESCAPE;
                t["esc"]       = VK_ESCAPE;
                t["tab"]       = VK_TAB;
                t["backspace"] = VK_BACK;
                t["capslock"]  = VK_CAPITAL;
                t["shift"]     = VK_SHIFT;
                t["lshift"]    = VK_LSHIFT;
                t["rshift"]    = VK_RSHIFT;
                t["ctrl"]      = VK_CONTROL;
                t["control"]   = VK_CONTROL;
                t["lctrl"]     = VK_LCONTROL;
                t["rctrl"]     = VK_RCONTROL;
                t["alt"]       = VK_MENU;
                t["lalt"]      = VK_LMENU;
                t["ralt"]      = VK_RMENU;
                t["up"]        = VK_UP;
                t["down"]      = VK_DOWN;
                t["left"]      = VK_LEFT;
                t["right"]     = VK_RIGHT;
                t["insert"]    = VK_INSERT;
                t["delete"]    = VK_DELETE;
                t["home"]      = VK_HOME;
                t["end"]       = VK_END;
                t["pageup"]    = VK_PRIOR;
                t["pagedown"]  = VK_NEXT;
                t["mouse1"]    = VK_LBUTTON;
                t["mouse2"]    = VK_RBUTTON;
                t["mouse3"]    = VK_MBUTTON;
                t["mouse4"]    = VK_XBUTTON1;
                t["mouse5"]    = VK_XBUTTON2;
                return t;
            }();
            return table;
        }

        int NameToVk(const std::string &lowerName) {
            const auto &table = KeyTable();
            const auto it      = table.find(lowerName);
            return it == table.end() ? -1 : it->second;
        }

        bool IsPhysicallyDown(int vk) {
            return (::GetAsyncKeyState(vk) & 0x8000) != 0;
        }
    } // anonymous namespace

    std::map<std::string, Keybinds::Bucket> Keybinds::_buckets;
    std::mutex Keybinds::_mutex;
    std::function<bool()> Keybinds::_activeCallback;
    Framework::Scripting::ResourceManager *Keybinds::_resourceManager = nullptr;

    void Keybinds::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> frameworkObj, Framework::Scripting::ResourceManager *resourceManager) {
        {
            std::scoped_lock lock(_mutex);
            _buckets.clear();
        }
        _resourceManager = resourceManager;

        v8::Local<v8::Object> keyObj = v8::Object::New(isolate);

        const auto bind = [&](const char *name, v8::FunctionCallback callback) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback);
            keyObj->Set(context, v8pp::to_v8(isolate, name), tmpl->GetFunction(context).ToLocalChecked()).Check();
        };

        bind("bind", BindCallback);
        bind("unbind", UnbindCallback);
        bind("isDown", IsDownCallback);

        frameworkObj->Set(context, v8pp::to_v8(isolate, "Key"), keyObj).Check();
    }

    void Keybinds::SetActiveCallback(std::function<bool()> callback) {
        std::scoped_lock lock(_mutex);
        _activeCallback = std::move(callback);
    }

    bool Keybinds::ParseState(const std::string &s, State &out) {
        if (s == "down") {
            out = State::Down;
            return true;
        }
        if (s == "up") {
            out = State::Up;
            return true;
        }
        if (s == "both") {
            out = State::Both;
            return true;
        }
        return false;
    }

    bool Keybinds::GateAllowed() {
        // Caller holds _mutex.
        return !_activeCallback || _activeCallback();
    }

    void Keybinds::BindCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2 || !args[0]->IsString()) {
            ThrowError(isolate, "Key.bind requires (key, state, handler) or (key, handler)");
            return;
        }
        if (!_resourceManager) {
            ThrowError(isolate, "Key.bind: resource manager not available");
            return;
        }

        // (key, handler) defaults to "down"; (key, state, handler) is explicit.
        State state = State::Down;
        v8::Local<v8::Function> handler;
        if (args[1]->IsFunction()) {
            handler = args[1].As<v8::Function>();
        }
        else if (args[1]->IsString() && args.Length() >= 3 && args[2]->IsFunction()) {
            if (!ParseState(v8pp::from_v8<std::string>(isolate, args[1]), state)) {
                ThrowError(isolate, "Key.bind: state must be 'down', 'up' or 'both'");
                return;
            }
            handler = args[2].As<v8::Function>();
        }
        else {
            ThrowError(isolate, "Key.bind: expected a handler function");
            return;
        }

        const std::string keyName = ToLower(v8pp::from_v8<std::string>(isolate, args[0]));
        const int vk               = NameToVk(keyName);
        if (vk < 0) {
            ThrowError(isolate, "Key.bind: unknown key name '" + keyName + "'");
            return;
        }

        const std::string resourceName = ResolveResourceName(isolate, _resourceManager, handler);
        if (resourceName.empty()) {
            ThrowError(isolate, "Key.bind: must be called from within a resource");
            return;
        }

        {
            std::scoped_lock lock(_mutex);
            auto &bucket = _buckets[keyName];
            if (bucket.handlers.empty()) {
                // Seed from live state so a held key doesn't fire on bind.
                bucket.vk       = vk;
                bucket.prevDown = IsPhysicallyDown(vk);
            }
            Handler entry;
            entry.callback.Reset(isolate, handler);
            entry.resourceName = resourceName;
            entry.state        = state;
            bucket.handlers.push_back(std::move(entry));
        }

        args.GetReturnValue().Set(true);
    }

    void Keybinds::UnbindCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Key.unbind requires (key, state?, handler?)");
            return;
        }

        const std::string keyName = ToLower(v8pp::from_v8<std::string>(isolate, args[0]));

        // Optional state filter, optional handler filter (in either order after the key).
        bool hasStateFilter = false;
        State stateFilter    = State::Down;
        v8::Local<v8::Function> handler;
        for (int i = 1; i < args.Length(); ++i) {
            if (args[i]->IsString()) {
                if (!ParseState(v8pp::from_v8<std::string>(isolate, args[i]), stateFilter)) {
                    ThrowError(isolate, "Key.unbind: state must be 'down', 'up' or 'both'");
                    return;
                }
                hasStateFilter = true;
            }
            else if (args[i]->IsFunction()) {
                handler = args[i].As<v8::Function>();
            }
        }

        bool removed = false;
        {
            std::scoped_lock lock(_mutex);
            auto it = _buckets.find(keyName);
            if (it != _buckets.end()) {
                auto &handlers    = it->second.handlers;
                const auto before = handlers.size();
                handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                   [&](const Handler &h) {
                                       if (hasStateFilter && h.state != stateFilter) {
                                           return false;
                                       }
                                       if (!handler.IsEmpty() && !h.callback.Get(isolate)->StrictEquals(handler)) {
                                           return false;
                                       }
                                       return true;
                                   }),
                    handlers.end());
                removed = handlers.size() != before;
                if (handlers.empty()) {
                    _buckets.erase(it);
                }
            }
        }

        args.GetReturnValue().Set(removed);
    }

    void Keybinds::IsDownCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Key.isDown requires a key name");
            return;
        }

        const std::string keyName = ToLower(v8pp::from_v8<std::string>(isolate, args[0]));
        const int vk               = NameToVk(keyName);
        if (vk < 0) {
            ThrowError(isolate, "Key.isDown: unknown key name '" + keyName + "'");
            return;
        }

        bool allowed;
        {
            std::scoped_lock lock(_mutex);
            allowed = GateAllowed();
        }
        args.GetReturnValue().Set(allowed && IsPhysicallyDown(vk));
    }

    void Keybinds::Update() {
        auto *manager = _resourceManager;
        if (!manager) {
            return;
        }

        // Phase 1: edge-detect without touching V8.
        struct Edge {
            std::string keyName;
            bool down;
        };
        std::vector<Edge> edges;
        {
            std::scoped_lock lock(_mutex);
            if (_buckets.empty()) {
                return;
            }
            const bool allowed = GateAllowed();
            for (auto &[keyName, bucket] : _buckets) {
                const bool down = IsPhysicallyDown(bucket.vk);
                if (down == bucket.prevDown) {
                    continue;
                }
                bucket.prevDown = down;
                // Suppressed: advance prevDown but don't dispatch.
                if (allowed) {
                    edges.push_back({keyName, down});
                }
            }
        }
        if (edges.empty()) {
            return;
        }

        // Phase 2: dispatch matching handlers.
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

        struct Pending {
            v8::Local<v8::Function> fn;
            std::string resourceName;
            std::string keyName;
            const char *stateStr;
        };
        std::vector<Pending> pending;
        {
            std::scoped_lock lock(_mutex);
            for (const auto &edge : edges) {
                auto it = _buckets.find(edge.keyName);
                if (it == _buckets.end()) {
                    continue;
                }
                const State edgeState = edge.down ? State::Down : State::Up;
                const char *stateStr   = edge.down ? "down" : "up";
                for (const auto &h : it->second.handlers) {
                    if (h.state == edgeState || h.state == State::Both) {
                        pending.push_back({h.callback.Get(isolate), h.resourceName, edge.keyName, stateStr});
                    }
                }
            }
        }

        for (const auto &p : pending) {
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> argv[] = {
                v8pp::to_v8(isolate, p.keyName),
                v8pp::to_v8(isolate, p.stateStr),
            };
            (void)p.fn->Call(context, context->Global(), 2, argv);
            if (tryCatch.HasCaught()) {
                const std::string error = Framework::Scripting::FormatV8Exception(isolate, tryCatch, "Unknown error in key bind handler");
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Key '{}' ({}) handler error: {}", p.resourceName, p.keyName, p.stateStr, error);
            }
        }
    }

    void Keybinds::CleanupResource(const std::string &resourceName) {
        std::scoped_lock lock(_mutex);
        for (auto it = _buckets.begin(); it != _buckets.end();) {
            auto &handlers = it->second.handlers;
            handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                               [&](const Handler &h) {
                                   return h.resourceName == resourceName;
                               }),
                handlers.end());
            if (handlers.empty()) {
                it = _buckets.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Keybinds::Shutdown() {
        std::scoped_lock lock(_mutex);
        _buckets.clear();
        _activeCallback = nullptr;
        _resourceManager = nullptr;
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
