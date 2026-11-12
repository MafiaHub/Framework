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

#include <scripting/lua/lua_runtime.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/script_log.h>
#include <scripting/scripting_catalog.h>
#include <scripting/transferred_value.h>

#include <core_modules.h>
#include <input/input.h>
#include <utils/key_names.h>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace Bind = Framework::Scripting::Bind;

    namespace {
        Framework::Scripting::TransferredValue Text(std::string text) {
            Framework::Scripting::TransferredValue value;
            value.kind = Framework::Scripting::TransferredValue::Kind::String;
            value.text = std::move(text);
            return value;
        }

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        bool IsKeyDown(int key) {
            const auto *input = CoreModules::GetInput();
            return input && input->IsKeyDown(key);
        }

        uint32_t GetLastWheelNotch() {
            const auto *input = CoreModules::GetInput();
            return input ? input->GetLastWheelNotch() : 0;
        }
    } // anonymous namespace

    std::map<std::string, Keybinds::Bucket> Keybinds::_buckets;
    std::mutex Keybinds::_mutex;
    std::function<bool()> Keybinds::_activeCallback;
    std::map<const Framework::Scripting::ResourceManager *, std::shared_ptr<Keybinds::ManagerLink>> Keybinds::_managers;
    bool Keybinds::_sourceWasUnavailable = false;
    uint32_t Keybinds::_wheelCursor      = 0;
    std::optional<uint32_t> Keybinds::_dispatchNotch;

    const Bind::Namespace &Keybinds::Definition() {
        using Bind::Doc;
        using Bind::Side;
        using Bind::TypeOf;
        using Bind::Type::Custom;

        static const auto definition = [] {
            const auto text    = TypeOf<std::string>();
            const auto boolean = TypeOf<bool>();
            const auto handler = Custom("KeyHandler", "fun(key: string, state: string)");
            return Bind::Namespace("Key", "Client-only, resource-owned physical key bindings exposed as the global Key.")
                .Function("getLabel", &GetLabel,
                    Doc("Returns the current layout's label for a physical key, even while UI owns input. Query again when refreshing prompts. Throws for unknown keys or non-string arguments.")
                        .Param("key", text, "Case-insensitive physical key name, using the same names as bind.")
                        .ReturnType(text)
                        .Returns("Uppercase printable text, or an English name such as Enter or Mouse 1. Falls back to the uppercase canonical key name when translation is unavailable."),
                    Side::Client)
                .Function("bind", &BindKey,
                    Doc("Binds a resource-owned handler that fires while the game has foreground input and no UI is capturing it. Each wheelup/wheeldown notch fires down, then up.")
                        .Param("key", text, "Case-insensitive supported keyboard or mouse key name.")
                        .Param("stateOrHandler", Custom("\"down\" | \"up\" | \"both\" | KeyHandler", "\"down\" | \"up\" | \"both\" | fun(key: string, state: string)"), "Trigger state, or the handler itself to use the default down state.")
                        .OptionalParam("handler", handler, "Handler required when an explicit trigger state is provided.")
                        .ReturnType(boolean)
                        .Returns("True after the binding is installed; invalid keys or states throw."),
                    Side::Client)
                .Function("unbind", &UnbindKey,
                    Doc("Removes matching bindings owned by the calling resource; omitting filters removes every binding for the key.")
                        .Param("key", text, "Case-insensitive supported key name.")
                        .OptionalParam("state", Custom("\"down\" | \"up\" | \"both\"", "\"down\" | \"up\" | \"both\""), "Optional trigger-state filter.")
                        .OptionalParam("handler", handler, "Optional exact handler filter.")
                        .ReturnType(boolean)
                        .Returns("True when at least one binding was removed."),
                    Side::Client)
                .Function("isDown", &IsDown,
                    Doc("Queries live physical key state using the same foreground and UI-input gate as binding dispatch.")
                        .Param("key", text, "Case-insensitive supported key name.")
                        .ReturnType(boolean)
                        .Returns("False when the key is up, the game is backgrounded, UI owns input, or the key name is invalid. Always false for the wheel, which is never held."),
                    Side::Client);
        }();
        return definition;
    }

    void Keybinds::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        {
            std::scoped_lock lock(_mutex);
            _buckets.clear();
        }
        _sourceWasUnavailable = false;
        _wheelCursor          = GetLastWheelNotch();

        // So the calls of this isolate name their resource and its manager.
        Bind::SetCallResourceManager(isolate, resourceManager);
        Definition().MaterialiseV8(isolate, context, target, Framework::Scripting::GetScriptingCatalog(isolate), Bind::Side::Client);
    }

    void Keybinds::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

    void Keybinds::GetLabel(Bind::Call &call) {
        if (call.ArgCount() < 1 || !call.Arg(0).IsString()) {
            call.ThrowError("Key.getLabel requires a key name");
            return;
        }
        const std::string keyName = call.Arg(0).AsString();
        const int key             = Utils::KeyNames::ToVirtualKey(keyName);
        if (key < 0) {
            call.ThrowError("Key.getLabel: unknown key name '" + keyName + "'");
            return;
        }
        call.Return(Utils::KeyNames::GetLabel(key));
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

    std::shared_ptr<Keybinds::ManagerLink> Keybinds::LinkOf(Framework::Scripting::ResourceManager *manager) {
        std::shared_ptr<ManagerLink> &link = _managers[manager];
        if (link == nullptr) {
            link          = std::make_shared<ManagerLink>();
            link->manager = manager;
        }
        return link;
    }

    void Keybinds::BindKey(Bind::Call &call) {
        if (call.ArgCount() < 2 || !call.Arg(0).IsString()) {
            call.ThrowError("Key.bind requires (key, state, handler) or (key, handler)");
            return;
        }
        Framework::Scripting::ResourceManager *manager = call.GetResourceManager();
        if (!manager) {
            call.ThrowError("Key.bind: resource manager not available");
            return;
        }

        // (key, handler) defaults to "down"; (key, state, handler) is explicit.
        State state        = State::Down;
        size_t handlerSlot = 1;
        if (!call.Arg(1).IsFunction()) {
            if (!call.Arg(1).IsString() || !call.Arg(2).IsFunction()) {
                call.ThrowError("Key.bind: expected a handler function");
                return;
            }
            if (!ParseState(call.Arg(1).AsString(), state)) {
                call.ThrowError("Key.bind: state must be 'down', 'up' or 'both'");
                return;
            }
            handlerSlot = 2;
        }

        const std::string keyName = ToLower(call.Arg(0).AsString());
        const int vk              = Utils::KeyNames::ToVirtualKey(keyName);
        if (vk < 0) {
            call.ThrowError("Key.bind: unknown key name '" + keyName + "'");
            return;
        }

        // The resource the handler belongs to, before the one that is calling.
        const std::string resourceName = call.OwnerOf(call.Arg(handlerSlot));
        if (resourceName.empty()) {
            call.ThrowError("Key.bind: must be called from within a resource");
            return;
        }

        std::shared_ptr<Bind::ScriptCallable> callback = call.Arg(handlerSlot).ToCallable();
        if (callback == nullptr) {
            return;
        }

        {
            std::scoped_lock lock(_mutex);
            auto &bucket = _buckets[keyName];
            if (bucket.handlers.empty()) {
                // Seed from live state so a held key doesn't fire on bind.
                bucket.vk       = vk;
                bucket.prevDown = IsKeyDown(vk);
            }
            Handler entry;
            entry.callback     = std::move(callback);
            entry.resourceName = resourceName;
            entry.manager      = LinkOf(manager);
            entry.state        = state;
            entry.boundNotch   = _dispatchNotch ? *_dispatchNotch : GetLastWheelNotch();
            bucket.handlers.push_back(std::move(entry));
        }

        call.Return(true);
    }

    void Keybinds::UnbindKey(Bind::Call &call) {
        if (call.ArgCount() < 1 || !call.Arg(0).IsString()) {
            call.ThrowError("Key.unbind requires (key, state?, handler?)");
            return;
        }

        const std::string keyName = ToLower(call.Arg(0).AsString());

        // Optional state filter, optional handler filter (in either order after the key).
        bool hasStateFilter = false;
        State stateFilter   = State::Down;
        Bind::Value handlerValue;
        for (size_t i = 1; i < call.ArgCount(); ++i) {
            if (call.Arg(i).IsString()) {
                if (!ParseState(call.Arg(i).AsString(), stateFilter)) {
                    call.ThrowError("Key.unbind: state must be 'down', 'up' or 'both'");
                    return;
                }
                hasStateFilter = true;
            }
            else if (call.Arg(i).IsFunction()) {
                handlerValue = call.Arg(i);
            }
        }

        // Only the caller's own bindings may go; resolved like Key.bind so the owner names match.
        const Framework::Scripting::ResourceManager *manager = call.GetResourceManager();
        if (!manager) {
            call.ThrowError("Key.unbind: resource manager not available");
            return;
        }
        const std::string caller = call.OwnerOf(handlerValue);
        if (caller.empty()) {
            call.Return(false);
            return;
        }
        // The same function gives the same callable, so the handler is found again by it.
        const std::shared_ptr<Bind::ScriptCallable> handler = handlerValue.ToCallable();

        bool removed = false;
        {
            std::scoped_lock lock(_mutex);
            auto it = _buckets.find(keyName);
            if (it != _buckets.end()) {
                auto &handlers    = it->second.handlers;
                const auto before = handlers.size();
                std::erase_if(handlers, [&](const Handler &h) {
                    if (h.resourceName != caller || h.manager->manager != manager) {
                        return false;
                    }
                    if (hasStateFilter && h.state != stateFilter) {
                        return false;
                    }
                    if (handler != nullptr && !h.callback->Equals(*handler)) {
                        return false;
                    }
                    return true;
                });
                removed = handlers.size() != before;
                if (handlers.empty()) {
                    _buckets.erase(it);
                }
            }
        }

        call.Return(removed);
    }

    void Keybinds::IsDown(Bind::Call &call) {
        if (call.ArgCount() < 1 || !call.Arg(0).IsString()) {
            call.ThrowError("Key.isDown requires a key name");
            return;
        }

        const std::string keyName = ToLower(call.Arg(0).AsString());
        const int vk              = Utils::KeyNames::ToVirtualKey(keyName);
        if (vk < 0) {
            call.ThrowError("Key.isDown: unknown key name '" + keyName + "'");
            return;
        }

        bool allowed;
        {
            std::scoped_lock lock(_mutex);
            allowed = GateAllowed();
        }
        call.Return(allowed && IsKeyDown(vk));
    }

    void Keybinds::Update() {
        // An unavailable source must not publish releases or manufacture presses when it
        // resumes. Hold the detector and seed it from the first fresh source state.
        const auto *input = CoreModules::GetInput();
        if (!input || !input->IsAvailable()) {
            _sourceWasUnavailable = true;
            return;
        }
        const bool resync     = _sourceWasUnavailable;
        _sourceWasUnavailable = false;

        const uint32_t lastNotch  = input->GetLastWheelNotch();
        const uint32_t newNotches = resync ? 0 : std::min(lastNotch - _wheelCursor, Framework::Input::IInput::kWheelNotchHistory);
        _wheelCursor              = lastNotch;

        // Phase 1: edge-detect without running any script.
        struct Edge {
            std::string keyName;
            bool down;
            uint32_t notch; // 0 for keys
        };
        std::vector<Edge> edges;
        {
            std::scoped_lock lock(_mutex);
            if (_buckets.empty()) {
                return;
            }
            const bool allowed = GateAllowed();
            for (auto &[keyName, bucket] : _buckets) {
                const bool down = IsKeyDown(bucket.vk);
                if (resync) {
                    bucket.prevDown = down;
                    continue;
                }
                if (down == bucket.prevDown) {
                    continue;
                }
                // A fresh key-down while Alt is held is a Windows system chord (Alt+Tab / Alt+F4 /
                // Alt+Enter / Alt+Space), not a script keystroke. Drop it WITHOUT latching prevDown so
                // the later release fires no stray "up" and a genuine press after Alt releases still
                // registers. Held keys (prevDown already set) and releases are unaffected.
                if (down && bucket.vk != FW_KEY_MENU && IsKeyDown(FW_KEY_MENU)) {
                    continue;
                }
                bucket.prevDown = down;
                // Suppressed: advance prevDown but don't dispatch.
                if (allowed) {
                    edges.push_back({keyName, down, 0});
                }
            }
            // Each wheel notch is a press and its release, in the order the notches arrived.
            for (uint32_t age = newNotches; allowed && age > 0; --age) {
                const uint32_t notch = lastNotch - age + 1;
                const int key        = input->GetWheelNotch(notch);
                for (const auto &[keyName, bucket] : _buckets) {
                    if (key != 0 && bucket.vk == key) {
                        edges.push_back({keyName, true, notch});
                        edges.push_back({keyName, false, notch});
                    }
                }
            }
        }
        if (edges.empty()) {
            return;
        }

        // Phase 2: dispatch matching handlers, each in the runtime that owns it.
        struct Pending {
            std::shared_ptr<Bind::ScriptCallable> callback;
            std::string resourceName;
            std::shared_ptr<ManagerLink> manager;
        };
        // Gate and handlers are resolved per edge, so a handler that unbinds or opens a UI holds back
        // the rest of this tick's edges.
        _dispatchNotch = lastNotch - newNotches;
        for (const auto &edge : edges) {
            if (edge.notch != 0) {
                _dispatchNotch = edge.notch;
            }
            const State edgeState = edge.down ? State::Down : State::Up;
            const char *stateStr  = edge.down ? "down" : "up";
            std::vector<Pending> pending;
            {
                std::scoped_lock lock(_mutex);
                auto it = _buckets.find(edge.keyName);
                if (it == _buckets.end() || !GateAllowed()) {
                    continue;
                }
                // A handler whose runtime is gone is dropped, never invoked.
                std::erase_if(it->second.handlers, [](const Handler &h) {
                    return !h.callback->IsAlive();
                });
                for (const auto &h : it->second.handlers) {
                    if ((h.state == edgeState || h.state == State::Both) && (edge.notch == 0 || edge.notch > h.boundNotch)) {
                        pending.push_back({h.callback, h.resourceName, h.manager});
                    }
                }
                if (it->second.handlers.empty()) {
                    _buckets.erase(it);
                }
            }

            const std::vector<Framework::Scripting::TransferredValue> args = {Text(edge.keyName), Text(stateStr)};
            for (const auto &p : pending) {
                // Its runtime may have gone with a handler that ran before it.
                if (!p.callback->IsAlive()) {
                    continue;
                }
                // A handler that fails is reported to the manager of its resource, when it threw and when
                // what it waited on failed later; one cut short because its runtime stopped has nothing
                // to report.
                p.callback->Invoke(args, Bind::ReturnMode::Outcome, [manager = p.manager, resourceName = p.resourceName, keyName = edge.keyName, stateStr](Bind::CallOutcome &&outcome) {
                    if (!outcome.threw || outcome.error == Bind::kRuntimeStopped) {
                        return;
                    }
                    const std::string message = fmt::format("Key '{}' ({}) handler error: {}", keyName, stateStr, outcome.error);
                    // Queued: the manager applies what the manifest asks for at its next upkeep.
                    if (manager->manager != nullptr) {
                        manager->manager->ReportHandlerFailure(resourceName, message);
                        return;
                    }
                    Framework::Scripting::LogScriptError(nullptr, resourceName, message);
                });
            }
        }
        _dispatchNotch.reset();
    }

    void Keybinds::CleanupResource(const std::string &resourceName) {
        CleanupResource(nullptr, resourceName);
    }

    void Keybinds::CleanupResource(const Framework::Scripting::ResourceManager *manager, const std::string &resourceName) {
        std::scoped_lock lock(_mutex);
        for (auto it = _buckets.begin(); it != _buckets.end();) {
            auto &handlers = it->second.handlers;
            std::erase_if(handlers, [&](const Handler &h) {
                return h.resourceName == resourceName && (manager == nullptr || h.manager->manager == manager);
            });
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
        _sourceWasUnavailable = false;
        // NOTE: do NOT reset _activeCallback here. It is a HOST gate (installed via SetActiveCallback)
        // that captures host state living for the whole process — well beyond a single scripting session.
        // Clearing it on every Shutdown (disconnect) dropped keybind suppression after a RECONNECT (the
        // host only re-installs the gate on a full process restart), so bound keys started firing again
        // while typing into chat. The host can still clear it explicitly via SetActiveCallback(nullptr).
        // A handler that is still running reports to nobody.
        for (auto &[manager, link] : _managers) {
            link->manager = nullptr;
        }
        _managers.clear();
    }

    size_t Keybinds::HandlerCount() {
        std::scoped_lock lock(_mutex);
        size_t count = 0;
        for (const auto &[keyName, bucket] : _buckets) {
            count += bucket.handlers.size();
        }
        return count;
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
