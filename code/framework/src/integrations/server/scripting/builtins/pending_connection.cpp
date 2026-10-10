/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "pending_connection.h"

#include "../../connection_gate.h"

#include <core_modules.h>
#include <scripting/scripting_catalog.h>

#include <v8pp/convert.hpp>

namespace Framework::Integrations::Server::Scripting::Builtins {
    namespace {
        // The gate is missing only if the server never brought it up: a state error, not a stale
        // handle, so it throws.
        ConnectionGate *ResolveGate(const v8::FunctionCallbackInfo<v8::Value> &info, const char *fn) {
            ConnectionGate *gate = CoreModules::GetConnectionGate();
            if (!gate) {
                v8::Isolate *isolate = info.GetIsolate();
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string(fn) + ": the connection gate is not running")));
            }
            return gate;
        }

        uint64_t GuidOf(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return info.Data().As<v8::BigInt>()->Uint64Value();
        }

        void DefineValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, const char *name, v8::Local<v8::Value> value) {
            const auto attributes = static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
            target->DefineOwnProperty(context, v8pp::to_v8(isolate, name), value, attributes).Check();
        }
    } // namespace

    v8::Local<v8::Object> PendingConnection::Create(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, const std::string &address) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Object> connection = v8::Object::New(isolate);

        DefineValue(isolate, context, connection, "nickname", v8pp::to_v8(isolate, identity.name));
        DefineValue(isolate, context, connection, "steamId", v8pp::to_v8(isolate, identity.steamId));
        DefineValue(isolate, context, connection, "epicId", v8pp::to_v8(isolate, identity.epicId));
        DefineValue(isolate, context, connection, "discordId", v8pp::to_v8(isolate, identity.discordId));
        DefineValue(isolate, context, connection, "hardwareId", v8pp::to_v8(isolate, identity.hardwareId));
        DefineValue(isolate, context, connection, "ticket", v8pp::to_v8(isolate, identity.ticket));
        DefineValue(isolate, context, connection, "ip", v8pp::to_v8(isolate, address));

        const v8::Local<v8::BigInt> data = v8::BigInt::NewFromUnsigned(isolate, guid);
        const auto method                = [&](const char *name, v8::FunctionCallback callback) {
            DefineValue(isolate, context, connection, name, v8::Function::New(context, callback, data).ToLocalChecked());
        };
        method("reject", &PendingConnection::JS_Reject);
        method("update", &PendingConnection::JS_Update);
        method("isPending", &PendingConnection::JS_IsPending);

        return handleScope.Escape(connection);
    }

    void PendingConnection::JS_Reject(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        if (info.Length() > 0 && !info[0]->IsUndefined() && !info[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "PendingConnection.reject: expected a string reason")));
            return;
        }
        ConnectionGate *gate = ResolveGate(info, "PendingConnection.reject");
        if (!gate) {
            return;
        }
        gate->Refuse(GuidOf(info), info.Length() > 0 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string());
    }

    void PendingConnection::JS_Update(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "PendingConnection.update: expected a string message")));
            return;
        }
        ConnectionGate *gate = ResolveGate(info, "PendingConnection.update");
        if (!gate) {
            return;
        }
        gate->SendStatus(GuidOf(info), v8pp::from_v8<std::string>(isolate, info[0]));
    }

    void PendingConnection::JS_IsPending(const v8::FunctionCallbackInfo<v8::Value> &info) {
        ConnectionGate *gate = ResolveGate(info, "PendingConnection.isPending");
        if (!gate) {
            return;
        }
        info.GetReturnValue().Set(gate->IsPending(GuidOf(info)));
    }

    void PendingConnection::Register(v8::Isolate *isolate) {
        auto &catalog = Framework::Scripting::GetScriptingCatalog(isolate);

        catalog.data_type("EventMap").add_property(ConnectionGate::kEventName, "[connection: PendingConnection]",
            "Dispatched when a player asks to join, before the connection exists: they hold no player slot, are not counted as online, and are sent nothing -- no resource list, no download, no body. The request waits until every "
            "handler has returned and every Promise a handler returned has settled, then it is let in if a player slot is free (otherwise it is refused as full). `connection.reject()` turns it away instead, and so does a handler "
            "that throws or rejects, or handlers that have not settled within the server's admission timeout (30 seconds by default, restarted by every `connection.update()`). With no handler at all, every request is let in at once.");

        auto &type = catalog.data_type("PendingConnection", "A player asking to join, handed to `playerConnecting`. epicId is verified by the server against an Epic-signed proof; other identifiers are client-reported and unverified.");
        type.add_property("nickname", "string", "The name the player asked to join under.", true);
        type.add_property("steamId", "string", "Steam identifier the client reported, or an empty string when it had none.", true);
        type.add_property("epicId", "string", "Epic Games account identifier verified by the server before this event, or an empty string for a connection without Epic authentication.", true);
        type.add_property("discordId", "string", "Discord identifier the client reported, or an empty string when it had none.", true);
        type.add_property("hardwareId", "string", "Framework hardware identifier the client reported, or an empty string when it had none.", true);
        type.add_property("ticket", "string",
            "The string the client was launched with, as the `ticket` of its launch link, or an empty string. The framework passes it through untouched; checking it -- against a one-time join ticket your own launcher or website issued, typically -- is "
            "the script's job.",
            true);
        type.add_property("ip", "string", "The remote address the connection comes from, without its port.", true);
        type.record(v8pp::metadata::function_of<v8::FunctionCallback>("reject",
            v8pp::metadata::docs("void", {v8pp::metadata::param("reason", "string", true, "Shown to the player as written, up to 512 bytes. Omitted, they read that the server refused the connection.")},
                "Turns the player away before they receive anything. Takes effect on the next server tick, whatever other handlers are still doing. Does nothing once the connection is decided or the player has left.")));
        type.record(v8pp::metadata::function_of<v8::FunctionCallback>("update",
            v8pp::metadata::docs("void", {v8pp::metadata::param("message", "string", false, "One line for the player's connecting screen, up to 256 bytes: 'Checking the whitelist', 'You are 4th in the queue'.")},
                "Shows the waiting player a line of status and restarts the admission timeout, so a queue that keeps updating its players is never timed out. Does nothing once the connection is decided or the player has left.")));
        type.record(v8pp::metadata::function_of<v8::FunctionCallback>("isPending",
            v8pp::metadata::docs("boolean", {}, "Checks whether this connection is still waiting on a decision.", "False once it was let in or turned away, or the player gave up and left; a queue drops such entries.")));
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
