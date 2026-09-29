/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "connection_gate.h"

#include "scripting/builtins/pending_connection.h"
#include "scripting/module.h"

#include <logging/logger.h>
#include <networking/network_server.h>
#include <scripting/builtins/events.h>
#include <scripting/node_engine.h>
#include <scripting/resource/resource_manager.h>

#include <v8pp/convert.hpp>

#include <utility>

namespace Framework::Integrations::Server {
    namespace {
        // What the player reads when a script refused without saying why, or the gate refused for
        // it. Never the script's error text: that is the server's business, not the player's.
        constexpr const char *kRefusedReason       = "The server refused the connection.";
        constexpr const char *kHandlerFailedReason = "The server could not check your connection. Try again later.";
        constexpr const char *kTimedOutReason      = "The server did not answer in time. Try again later.";

        // Cut to at most max bytes without splitting a UTF-8 sequence, so the player is never shown
        // a replacement character where a script wrote an accented letter.
        void TruncateUtf8(std::string &text, std::size_t max) {
            if (text.size() <= max) {
                return;
            }
            std::size_t end = max;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
                --end;
            }
            text.resize(end);
        }

        std::string ToText(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
            v8::TryCatch tryCatch(isolate);
            if (value->IsObject()) {
                v8::Local<v8::Value> stack;
                if (value.As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "stack")).ToLocal(&stack) && stack->IsString()) {
                    return v8pp::from_v8<std::string>(isolate, stack);
                }
            }
            v8::Local<v8::String> text;
            if (!value->ToString(context).ToLocal(&text)) {
                return "<unprintable>";
            }
            return v8pp::from_v8<std::string>(isolate, text);
        }
    } // namespace

    void ConnectionGate::Init(Scripting::ServerScriptingModule *scripting, Networking::NetworkServer *server, std::chrono::milliseconds timeout) {
        _scripting = scripting;
        _server    = server;
        _timeout   = timeout;
    }

    bool ConnectionGate::Begin(MafiaNet::RakNetGUID guid, const Networking::RPC::ClientIdentity &identity, const std::string &address) {
        auto *engine          = _scripting ? _scripting->GetEngine() : nullptr;
        auto *resourceManager = _scripting ? _scripting->GetResourceManager() : nullptr;
        if (!engine || !resourceManager || !engine->IsInitialized()) {
            return false;
        }
        auto &events = resourceManager->GetEvents();
        if (events.GetListenerCount(kEventName) == 0) {
            return false;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        // In the table before any handler runs: one that calls reject() synchronously has to find it.
        Pending &pending = _pending[guid.g];
        pending.guid     = guid;
        pending.nickname = identity.name;
        pending.deadline = std::chrono::steady_clock::now() + _timeout;

        // Before the emit: a handler that answers synchronously logs its answer inside it.
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} is waiting on {}", identity.name, guid.g, kEventName);

        const v8::Local<v8::Object> connection = Scripting::Builtins::PendingConnection::Create(isolate, context, guid.g, identity, address);
        const v8::Local<v8::Promise> verdict   = events.EmitReserved(isolate, context, kEventName, {connection});

        // Looked up again: nothing a handler can reach inserts into the table, but it is not this
        // function's to assume the reference survived arbitrary script.
        if (const auto it = _pending.find(guid.g); it != _pending.end()) {
            it->second.verdict.Reset(isolate, verdict);
        }
        return true;
    }

    bool ConnectionGate::IsPending(uint64_t guid) const {
        const auto it = _pending.find(guid);
        return it != _pending.end() && !it->second.refusal.has_value();
    }

    void ConnectionGate::Refuse(uint64_t guid, std::string reason) {
        const auto it = _pending.find(guid);
        if (it == _pending.end() || it->second.refusal.has_value()) {
            return;
        }
        TruncateUtf8(reason, kMaxReasonLength);
        it->second.refusal = reason.empty() ? std::string(kRefusedReason) : std::move(reason);
    }

    bool ConnectionGate::SendStatus(uint64_t guid, std::string message) {
        const auto it = _pending.find(guid);
        if (it == _pending.end() || it->second.refusal.has_value() || !_server) {
            return false;
        }
        TruncateUtf8(message, kMaxStatusLength);

        // MafiaNet restarts its own session timer on both ends with this; the gate restarts its own:
        // a script still talking to the player is still working on them.
        _server->SendSessionStatus(it->second.guid, message);
        it->second.deadline = std::chrono::steady_clock::now() + _timeout;
        return true;
    }

    void ConnectionGate::Drop(uint64_t guid) {
        const auto it = _pending.find(guid);
        if (it == _pending.end()) {
            return;
        }

        auto *engine = _scripting ? _scripting->GetEngine() : nullptr;
        if (engine && engine->IsInitialized() && _scripting->GetResourceManager()) {
            v8::Isolate *isolate = engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            if (!it->second.verdict.IsEmpty()) {
                _scripting->GetResourceManager()->GetEvents().CancelPendingEmission(isolate, it->second.verdict.Get(isolate));
            }
            it->second.verdict.Reset();
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} left while waiting on {}", it->second.nickname, guid, kEventName);
        _pending.erase(it);
    }

    void ConnectionGate::Collect(std::vector<AdmissionDecision> &out) {
        if (_pending.empty()) {
            return;
        }
        auto *engine          = _scripting ? _scripting->GetEngine() : nullptr;
        auto *resourceManager = _scripting ? _scripting->GetResourceManager() : nullptr;
        if (!engine || !resourceManager || !engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        const auto now = std::chrono::steady_clock::now();
        for (auto it = _pending.begin(); it != _pending.end();) {
            Pending &pending                     = it->second;
            const v8::Local<v8::Promise> verdict = pending.verdict.Get(isolate);
            const v8::Promise::PromiseState state = verdict.IsEmpty() ? v8::Promise::PromiseState::kPending : verdict->State();

            AdmissionDecision decision {pending.guid, false, {}};
            if (pending.refusal.has_value()) {
                decision.reason = *pending.refusal;
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} refused: {}", pending.nickname, it->first, decision.reason);
            }
            else if (state == v8::Promise::PromiseState::kFulfilled) {
                decision.admitted = true;
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} admitted", pending.nickname, it->first);
            }
            else if (state == v8::Promise::PromiseState::kRejected) {
                decision.reason = kHandlerFailedReason;
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Player {} guid {} refused: a {} handler failed:\n{}", pending.nickname, it->first, kEventName, DescribeRejection(isolate, context, verdict->Result()));
            }
            else if (now >= pending.deadline) {
                decision.reason = kTimedOutReason;
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Player {} guid {} refused: {} handlers did not settle within {} ms", pending.nickname, it->first, kEventName, _timeout.count());
            }
            else {
                ++it;
                continue;
            }

            // Handlers still running for a decided connection are left to finish; only the
            // native resolver waiting on them is released.
            if (state == v8::Promise::PromiseState::kPending && !verdict.IsEmpty()) {
                resourceManager->GetEvents().CancelPendingEmission(isolate, verdict);
            }
            pending.verdict.Reset();
            out.push_back(std::move(decision));
            it = _pending.erase(it);
        }
    }

    void ConnectionGate::Shutdown() {
        auto *engine = _scripting ? _scripting->GetEngine() : nullptr;
        if (!_pending.empty() && engine && engine->IsInitialized()) {
            v8::Isolate *isolate = engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            for (auto &[guid, pending] : _pending) {
                pending.verdict.Reset();
            }
        }
        _pending.clear();
    }

    std::string ConnectionGate::DescribeRejection(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
        // The emission rejects with an AggregateError over every handler that failed; its own
        // message says nothing, the entries do.
        if (reason->IsObject()) {
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> errors;
            if (reason.As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "errors")).ToLocal(&errors) && errors->IsArray()) {
                const v8::Local<v8::Array> list = errors.As<v8::Array>();
                std::string text;
                for (uint32_t i = 0; i < list->Length(); ++i) {
                    v8::Local<v8::Value> entry;
                    if (!list->Get(context, i).ToLocal(&entry)) {
                        continue;
                    }
                    if (!text.empty()) {
                        text += '\n';
                    }
                    text += ToText(isolate, context, entry);
                }
                if (!text.empty()) {
                    return text;
                }
            }
        }
        return ToText(isolate, context, reason);
    }
} // namespace Framework::Integrations::Server
