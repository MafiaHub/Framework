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
#include <scripting/bind/v8_backend.h>
#include <scripting/lua/lua_runtime.h>
#include <scripting/scripting_catalog.h>
#include <scripting/value_transfer.h>

#include <v8pp/convert.hpp>

#include <cstring>
#include <stdexcept>
#include <utility>

namespace Framework::Integrations::Server::Scripting::Builtins {
    namespace Bind = Framework::Scripting::Bind;

    namespace {
        // The gate is missing only if the server never brought it up: a state error, not a stale
        // handle, so it throws.
        ConnectionGate *ResolveGate(Bind::Call &call, const char *fn) {
            ConnectionGate *gate = CoreModules::GetConnectionGate();
            if (!gate) {
                call.ThrowError(std::string(fn) + ": the connection gate is not running");
            }
            return gate;
        }

        // The three calls, on the connection `guid` names. A JavaScript object carries the guid in
        // each of its functions, a Lua handle in itself.
        void Reject(Bind::Call &call, uint64_t guid) {
            // A reason left out is none; null is not a reason.
            const Bind::Value reason = call.Arg(0);
            if (!reason.IsString() && (!reason.IsNil() || reason.IsNull())) {
                call.ThrowTypeError("PendingConnection.reject: expected a string reason");
                return;
            }
            ConnectionGate *gate = ResolveGate(call, "PendingConnection.reject");
            if (!gate) {
                return;
            }
            gate->Refuse(guid, reason.AsString());
        }

        void Update(Bind::Call &call, uint64_t guid) {
            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("PendingConnection.update: expected a string message");
                return;
            }
            ConnectionGate *gate = ResolveGate(call, "PendingConnection.update");
            if (!gate) {
                return;
            }
            gate->SendStatus(guid, call.Arg(0).AsString());
        }

        void IsPending(Bind::Call &call, uint64_t guid) {
            ConnectionGate *gate = ResolveGate(call, "PendingConnection.isPending");
            if (!gate) {
                return;
            }
            call.Return(gate->IsPending(guid));
        }

        // A call as a method of the class: on the connection of its receiver.
        template <void (*Function)(Bind::Call &, uint64_t)>
        void OnReceiver(Bind::Call &call) {
            if (const auto *self = call.Self<PendingConnection>()) {
                Function(call, self->GetGuid());
            }
        }

        // A call as a function of the JavaScript object of a connection.
        template <void (*Function)(Bind::Call &, uint64_t)>
        v8::Local<v8::Value> OnConnection(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid) {
            return Bind::MakeV8Function(isolate, context, [guid](Bind::Call &call) {
                Function(call, guid);
            });
        }

        void DefineValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, const char *name, v8::Local<v8::Value> value) {
            const auto attributes = static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
            target->DefineOwnProperty(context, v8pp::to_v8(isolate, name), value, attributes).Check();
        }

        // Where the JavaScript object of a connection keeps its guid: what tells the transfer that
        // an object is one, under a key no script can name.
        v8::Local<v8::Private> GuidKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.pendingConnection.guid"));
        }
    } // namespace

    PendingConnection::PendingConnection(uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, std::string address): _guid(guid), _identity(identity), _address(std::move(address)) {}

    PendingConnection::PendingConnection(uint64_t guid): _guid(guid) {
        const ConnectionGate *gate = CoreModules::GetConnectionGate();
        if (!gate || !gate->Describe(guid, _identity, _address)) {
            throw std::runtime_error("PendingConnection: the connection is no longer waiting on a decision");
        }
    }

    const Bind::Class<PendingConnection> &PendingConnection::Definition() {
        using Bind::Doc;
        using Bind::Side;
        using Bind::TypeOf;

        static const auto definition =
            Bind::Class<PendingConnection>("PendingConnection", "A player asking to join, handed to `playerConnecting`. Every identifier is reported by the player's own client and is not verified by the server.")
                .Property("nickname", &PendingConnection::GetNickname, "The name the player asked to join under.", Side::Server)
                .Property("steamId", &PendingConnection::GetSteamId, "Steam identifier the client reported, or an empty string when it had none.", Side::Server)
                .Property("epicId", &PendingConnection::GetEpicId, "Epic Games account identifier the client reported, or an empty string when it had none.", Side::Server)
                .Property("discordId", &PendingConnection::GetDiscordId, "Discord identifier the client reported, or an empty string when it had none.", Side::Server)
                .Property("discordName", &PendingConnection::GetDiscordName, "Discord username the client reported, or an empty string when it had none.", Side::Server)
                .Property("hardwareId", &PendingConnection::GetHardwareId, "Framework hardware identifier the client reported, or an empty string when it had none.", Side::Server)
                .Property("ticket", &PendingConnection::GetTicket,
                    "The string the client was launched with, as the `ticket` of its launch link, or an empty string. The framework passes it through untouched; checking it -- against a one-time join ticket your own launcher or website issued, typically -- is "
                    "the script's job.",
                    Side::Server)
                .Property("ip", &PendingConnection::GetIp, "The remote address the connection comes from, without its port.", Side::Server)
                // Context functions: a reason may be left out, and each says in its own words what it was given instead.
                .Method("reject", &OnReceiver<&Reject>,
                    Doc("Turns the player away before they receive anything. Takes effect on the next server tick, whatever other handlers are still doing. Does nothing once the connection is decided or the player has left.")
                        .OptionalParam("reason", TypeOf<std::string>(), "Shown to the player as written, up to 512 bytes. Omitted, they read that the server refused the connection."),
                    Side::Server)
                .Method("update", &OnReceiver<&Update>,
                    Doc("Shows the waiting player a line of status and restarts the admission timeout, so a queue that keeps updating its players is never timed out. Does nothing once the connection is decided or the player has left.")
                        .Param("message", TypeOf<std::string>(), "One line for the player's connecting screen, up to 256 bytes: 'Checking the whitelist', 'You are 4th in the queue'."),
                    Side::Server)
                .Method("isPending", &OnReceiver<&IsPending>, Doc("Checks whether this connection is still waiting on a decision.").ReturnType(TypeOf<bool>()).Returns("False once it was let in or turned away, or the player gave up and left; a queue drops such entries."),
                    Side::Server)
                // Crosses between runtimes as its guid, and is who the gate holds under it where it arrives.
                .Transfer(&PendingConnection::GetGuid);
        return definition;
    }

    v8::Local<v8::Object> PendingConnection::ToObject(v8::Isolate *isolate, v8::Local<v8::Context> context) const {
        v8::EscapableHandleScope handleScope(isolate);
        // A plain object to a script, with one internal field it never sees: the transfer asks its host
        // types about native-backed objects only, and tells those by their internal fields.
        v8::Local<v8::ObjectTemplate> shape = v8::ObjectTemplate::New(isolate);
        shape->SetInternalFieldCount(1);
        v8::Local<v8::Object> connection = shape->NewInstance(context).ToLocalChecked();
        // An instance of a template has a prototype of its own; a plain object has the one of every object.
        connection->SetPrototypeV2(context, v8::Object::New(isolate)->GetPrototypeV2()).Check();

        DefineValue(isolate, context, connection, "nickname", v8pp::to_v8(isolate, _identity.name));
        DefineValue(isolate, context, connection, "steamId", v8pp::to_v8(isolate, _identity.steamId));
        DefineValue(isolate, context, connection, "epicId", v8pp::to_v8(isolate, _identity.epicId));
        DefineValue(isolate, context, connection, "discordId", v8pp::to_v8(isolate, _identity.discordId));
        DefineValue(isolate, context, connection, "discordName", v8pp::to_v8(isolate, _identity.discordName));
        DefineValue(isolate, context, connection, "hardwareId", v8pp::to_v8(isolate, _identity.hardwareId));
        DefineValue(isolate, context, connection, "ticket", v8pp::to_v8(isolate, _identity.ticket));
        DefineValue(isolate, context, connection, "ip", v8pp::to_v8(isolate, _address));

        DefineValue(isolate, context, connection, "reject", OnConnection<&Reject>(isolate, context, _guid));
        DefineValue(isolate, context, connection, "update", OnConnection<&Update>(isolate, context, _guid));
        DefineValue(isolate, context, connection, "isPending", OnConnection<&IsPending>(isolate, context, _guid));

        connection->SetPrivate(context, GuidKey(isolate), v8::BigInt::NewFromUnsigned(isolate, _guid)).Check();

        return handleScope.Escape(connection);
    }

    v8::Local<v8::Object> PendingConnection::Create(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, const std::string &address) {
        return PendingConnection(guid, identity, address).ToObject(isolate, context);
    }

    void PendingConnection::RegisterTransfer() {
        static bool registered = false;
        if (registered) {
            return;
        }
        registered = true;

        // Both languages, as the definition declares it: a Lua handle crosses as its guid.
        Definition().RegisterTransfer();

        // JavaScript has no object of the class: a connection is a plain object there, told by the
        // guid it carries, and built again as one.
        Framework::Scripting::ValueTransfer::RegisterHostType(
            Definition().Describe().name,
            [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                v8::Local<v8::Value> carried;
                if (!object->GetPrivate(isolate->GetCurrentContext(), GuidKey(isolate)).ToLocal(&carried) || !carried->IsBigInt()) {
                    return false;
                }
                const uint64_t guid = carried.As<v8::BigInt>()->Uint64Value();
                bytes.resize(sizeof(guid));
                std::memcpy(bytes.data(), &guid, sizeof(guid));
                return true;
            },
            [](v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                uint64_t guid = 0;
                std::memcpy(&guid, bytes.data(), std::min(bytes.size(), sizeof(guid)));
                try {
                    return PendingConnection(guid).ToObject(isolate, context);
                }
                catch (const std::exception &exception) {
                    isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string(exception.what()))));
                    return {};
                }
            });
    }

    void PendingConnection::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().MaterialiseLua(runtime.State(), runtime.Options().side);
    }

    void PendingConnection::Register(v8::Isolate *isolate) {
        auto &catalog = Framework::Scripting::GetScriptingCatalog(isolate);

        catalog.data_type("EventMap")
            .add_property(ConnectionGate::kEventName, "[connection: PendingConnection]",
                "Dispatched when a player asks to join, before the connection exists: they hold no player slot, are not counted as online, and are sent nothing -- no resource list, no download, no body. The request waits until every "
                "handler has returned and every Promise a handler returned has settled, then it is let in if a player slot is free (otherwise it is refused as full). `connection.reject()` turns it away instead, and so does a handler "
                "that throws or rejects, or handlers that have not settled within the server's admission timeout (30 seconds by default, restarted by every `connection.update()`). With no handler at all, every request is let in at once.");

        // The type is described the way the definition declares it. It is recorded as a data type and
        // not as a class: JavaScript has no constructor of it, only the objects the gate hands out.
        const Bind::ClassDesc &described = Definition().Describe();
        auto &type                       = catalog.data_type(described.name, described.description);
        for (const Bind::PropertyDesc &property : described.properties) {
            type.add_property(property.name, Bind::ToTypeScript(property.type), property.description, property.readonly);
        }
        for (const Bind::FunctionDesc &function : described.functions) {
            type.record(v8pp::metadata::function_of<v8::FunctionCallback>(function.name, Bind::Detail::V8FunctionOptions(function)));
        }
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
