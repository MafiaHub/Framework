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
#include <networking/account_key.h>
#include <scripting/builtins/player.h>
#include <scripting/engine_helpers.h>
#include <scripting/scripting_catalog.h>

#include <mafianet/bit_stream.h>
#include <scripting/value_transfer.h>
#include <v8pp/convert.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

        // What a connection is rebuilt from in another resource, held on the object where scripts cannot reach it.
        v8::Local<v8::Private> TransferKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8pp::to_v8(isolate, "Framework::PendingConnection"));
        }

        std::string EncodeTransfer(uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, const std::string &address) {
            MafiaNet::BitStream bs;
            bs.Write(guid);
            bs.Write(address);
            auto copy = identity;
            bs.Write(copy.Encode());
            return std::string(reinterpret_cast<const char *>(bs.GetData()), bs.GetNumberOfBytesUsed());
        }

        void RegisterTransfer() {
            static bool registered = false;
            if (std::exchange(registered, true)) {
                return;
            }
            Framework::Scripting::ValueTransfer::RegisterHostType(
                "PendingConnection",
                [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                    v8::Local<v8::Value> held;
                    if (!object->GetPrivate(isolate->GetCurrentContext(), TransferKey(isolate)).ToLocal(&held) || !held->IsArrayBuffer()) {
                        return false;
                    }
                    const std::shared_ptr<v8::BackingStore> store = held.As<v8::ArrayBuffer>()->GetBackingStore();
                    const auto *data                              = static_cast<const uint8_t *>(store->Data());
                    bytes.assign(data, data + store->ByteLength());
                    return true;
                },
                [](v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                    MafiaNet::BitStream bs(const_cast<unsigned char *>(bytes.data()), static_cast<unsigned int>(bytes.size()), false);
                    uint64_t guid = 0;
                    std::string address;
                    std::string encoded;
                    if (!bs.Read(guid) || !bs.Read(address) || !bs.Read(encoded)) {
                        return {};
                    }
                    const auto identity = Framework::Networking::RPC::ClientIdentity::Decode(encoded);
                    if (!identity) {
                        return {};
                    }
                    return PendingConnection::Create(isolate, context, guid, *identity, address);
                });
        }
    } // namespace

    v8::Local<v8::Object> PendingConnection::Create(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, const std::string &address) {
        v8::EscapableHandleScope handleScope(isolate);
        // One internal field: the value transfer only offers objects with one to its host types.
        const v8::Local<v8::ObjectTemplate> shape = v8::ObjectTemplate::New(isolate);
        shape->SetInternalFieldCount(1);
        v8::Local<v8::Object> connection = shape->NewInstance(context).ToLocalChecked();

        using Framework::Scripting::Builtins::Player;
        DefineValue(isolate, context, connection, "nickname", v8pp::to_v8(isolate, identity.name));
        DefineValue(isolate, context, connection, "accountId", v8pp::to_v8(isolate, Framework::Networking::AccountIdOf(identity.publicKey)));
        DefineValue(isolate, context, connection, "unverified", Player::NewUnverifiedIdentity(isolate, context, &identity));
        DefineValue(isolate, context, connection, "ticket", v8pp::to_v8(isolate, identity.ticket));
        // Non-enumerable, so spreading or serialising the connection does not trip them.
        for (const char *field : Player::kMovedIdentityFields) {
            const v8::Local<v8::Function> getter = Player::MovedIdentityGetter(isolate, field)->GetFunction(context).ToLocalChecked();
            connection->SetAccessorProperty(v8pp::to_v8(isolate, field).As<v8::Name>(), getter, v8::Local<v8::Function>(), static_cast<v8::PropertyAttribute>(v8::DontEnum | v8::DontDelete));
        }
        DefineValue(isolate, context, connection, "ip", v8pp::to_v8(isolate, address));

        const v8::Local<v8::BigInt> data = v8::BigInt::NewFromUnsigned(isolate, guid);
        const auto method                = [&](const char *name, v8::FunctionCallback callback) {
            DefineValue(isolate, context, connection, name, v8::Function::New(context, callback, data).ToLocalChecked());
        };
        method("reject", &PendingConnection::JS_Reject);
        method("update", &PendingConnection::JS_Update);
        method("isPending", &PendingConnection::JS_IsPending);

        const std::string transfer              = EncodeTransfer(guid, identity, address);
        std::shared_ptr<v8::BackingStore> store = v8::ArrayBuffer::NewBackingStore(isolate, transfer.size());
        std::memcpy(store->Data(), transfer.data(), transfer.size());
        connection->SetPrivate(context, TransferKey(isolate), v8::ArrayBuffer::New(isolate, store)).Check();

        return handleScope.Escape(connection);
    }

    void PendingConnection::JS_Reject(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);

        if (info.Length() > 0 && !info[0]->IsUndefined() && !info[0]->IsString()) {
            Framework::Scripting::ThrowTypeError(isolate, "PendingConnection.reject: expected a string reason");
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
            Framework::Scripting::ThrowTypeError(isolate, "PendingConnection.update: expected a string message");
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
        RegisterTransfer();
        auto &catalog = Framework::Scripting::GetScriptingCatalog(isolate);

        catalog.data_type("EventMap")
            .add_property(ConnectionGate::kEventName, "[connection: PendingConnection]",
                "Dispatched when a player asks to join, before the connection exists: they hold no player slot, are not counted as online, and are sent nothing -- no resource list, no download, no body. The request waits until every "
                "handler has returned and every Promise a handler returned has settled, then it is let in if a player slot is free (otherwise it is refused as full). `connection.reject()` turns it away instead, and so does a handler "
                "that throws or rejects, or handlers that have not settled within the server's admission timeout (30 seconds by default, restarted by every `connection.update()`). With no handler at all, every request is let in at once.");

        Framework::Scripting::Builtins::Player::DocumentUnverifiedIdentity(isolate);
        auto &type = catalog.data_type("PendingConnection",
            "A player asking to join, handed to `playerConnecting`. `accountId` is proven: the server checked the request was signed with that account's key before raising the event. Everything else is what the player's client says about itself.");
        type.add_property("nickname", "string", "The name the player asked to join under.", true);
        type.add_property("accountId", "string", "The account the player proved, as `player.accountId` will read once they are in. Check bans and whitelists against it.", true);
        type.add_property("unverified", "UnverifiedIdentity", "The Steam, Epic and Discord ids, Discord name and hardware id the client reported. Not proof of anything; see UnverifiedIdentity.", true);
        type.add_property("ticket", "string",
            "The string the client was launched with, as the `ticket` of its launch link, or an empty string. The framework passes it through untouched; checking it -- against a one-time join ticket your own launcher or website issued, typically -- is "
            "the script's job.",
            true);
        type.add_property("ip", "string", "The remote address the connection comes from, without its port.", true);
        type.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("reject", v8pp::metadata::docs("void", {v8pp::metadata::param("reason", "string", true, "Shown to the player as written, up to 512 bytes. Omitted, they read that the server refused the connection.")},
                                                                            "Turns the player away before they receive anything. Takes effect on the next server tick, whatever other handlers are still doing. Does nothing once the connection is decided or the player has left.")));
        type.record(v8pp::metadata::function_of<v8::FunctionCallback>("update",
            v8pp::metadata::docs("void", {v8pp::metadata::param("message", "string", false, "One line for the player's connecting screen, up to 256 bytes: 'Checking the whitelist', 'You are 4th in the queue'.")},
                "Shows the waiting player a line of status and restarts the admission timeout, so a queue that keeps updating its players is never timed out. Does nothing once the connection is decided or the player has left.")));
        type.record(v8pp::metadata::function_of<v8::FunctionCallback>("isPending",
            v8pp::metadata::docs("boolean", {}, "Checks whether this connection is still waiting on a decision.", "False once it was let in or turned away, or the player gave up and left; a queue drops such entries.")));
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
