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
#include <scripting/scripting_catalog.h>

#include <logging/logger.h>
#include <utils/key_names.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cctype>
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

        bool IsPhysicallyDown(int vk) {
            if ((::GetAsyncKeyState(vk) & 0x8000) == 0) {
                return false;
            }
            // GetAsyncKeyState is a GLOBAL key state — it reads the key even while our window is in the
            // background (alt-tab). Honor the documented "false while backgrounded" by requiring our own
            // process to own the foreground window.
            DWORD pid = 0;
            ::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
            return pid == ::GetCurrentProcessId();
        }
    } // anonymous namespace

    std::map<std::string, Keybinds::Bucket> Keybinds::_buckets;
    std::mutex Keybinds::_mutex;
    std::function<bool()> Keybinds::_activeCallback;
    Framework::Scripting::ResourceManager *Keybinds::_resourceManager = nullptr;

    void Keybinds::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
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

        target->Set(context, v8pp::to_v8(isolate, "Key"), keyObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Key", "Client-only, resource-owned physical key bindings exposed as the global Key.");
        metadata.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("bind", v8pp::metadata::docs("boolean",
                                                                          {
                                                                              v8pp::metadata::param("key", "string", false, "Case-insensitive supported keyboard or mouse key name."),
                                                                              v8pp::metadata::param("stateOrHandler", "\"down\" | \"up\" | \"both\" | ((key: string, state: \"down\" | \"up\") => void)", false, "Trigger state, or the handler itself to use the default down state."),
                                                                              v8pp::metadata::param("handler", "(key: string, state: \"down\" | \"up\") => void", true, "Handler required when an explicit trigger state is provided."),
                                                                          },
                                                                          "Binds a resource-owned handler that fires while the game has foreground input and no UI is capturing it.", "True after the binding is installed; invalid keys or states throw.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("unbind", v8pp::metadata::docs("boolean",
                                                                                        {
                                                                                            v8pp::metadata::param("key", "string", false, "Case-insensitive supported key name."),
                                                                                            v8pp::metadata::param("state", "\"down\" | \"up\" | \"both\"", true, "Optional trigger-state filter."),
                                                                                            v8pp::metadata::param("handler", "(key: string, state: \"down\" | \"up\") => void", true, "Optional exact handler filter."),
                                                                                        },
                                                                                        "Removes matching bindings owned by the calling resource; omitting filters removes every binding for the key.", "True when at least one binding was removed.")));
        metadata.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("isDown", v8pp::metadata::docs("boolean",
                                                                            {
                                                                                v8pp::metadata::param("key", "string", false, "Case-insensitive supported key name."),
                                                                            },
                                                                            "Queries live physical key state using the same foreground and UI-input gate as binding dispatch.", "False when the key is up, the game is backgrounded, UI owns input, or the key name is invalid.")));
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
        const int vk              = Utils::KeyNames::ToVirtualKey(keyName);
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
        State stateFilter   = State::Down;
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
        const int vk              = Utils::KeyNames::ToVirtualKey(keyName);
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
                // A fresh key-down while Alt is held is a Windows system chord (Alt+Tab / Alt+F4 /
                // Alt+Enter / Alt+Space), not a script keystroke. Drop it WITHOUT latching prevDown so
                // the later release fires no stray "up" and a genuine press after Alt releases still
                // registers. Held keys (prevDown already set) and releases are unaffected.
                if (down && bucket.vk != VK_MENU && (::GetAsyncKeyState(VK_MENU) & 0x8000)) {
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
                const char *stateStr  = edge.down ? "down" : "up";
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
        // NOTE: do NOT reset _activeCallback here. It is a HOST gate (installed via SetActiveCallback)
        // that captures host state living for the whole process — well beyond a single scripting session.
        // Clearing it on every Shutdown (disconnect) dropped keybind suppression after a RECONNECT (the
        // host only re-installs the gate on a full process restart), so bound keys started firing again
        // while typing into chat. The host can still clear it explicitly via SetActiveCallback(nullptr).
        _resourceManager = nullptr;
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
