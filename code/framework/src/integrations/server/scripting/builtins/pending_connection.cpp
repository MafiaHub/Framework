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
#include <scripting/scripting_catalog.h>
#include <scripting/value_transfer.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace Framework::Integrations::Server::Scripting::Builtins {
    namespace {
        // The gate is missing only if the server never brought it up: a state error, not a stale
        // handle, so it throws.
        ConnectionGate *ResolveGate(Framework::Scripting::Bind::Call &call, const char *fn) {
            ConnectionGate *gate = CoreModules::GetConnectionGate();
            if (!gate) {
                call.ThrowError(std::string(fn) + ": the connection gate is not running");
            }
            return gate;
        }

        // The three calls, on the connection `guid` names. A JavaScript object carries the guid in
        // each of its functions, a Lua handle in itself.
        void Reject(Framework::Scripting::Bind::Call &call, uint64_t guid) {
            // A reason left out is none; null is not a reason.
            const Framework::Scripting::Bind::Value reason = call.Arg(0);
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

        void Update(Framework::Scripting::Bind::Call &call, uint64_t guid) {
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

        void IsPending(Framework::Scripting::Bind::Call &call, uint64_t guid) {
            ConnectionGate *gate = ResolveGate(call, "PendingConnection.isPending");
            if (!gate) {
                return;
            }
            call.Return(gate->IsPending(guid));
        }

        // A call as a method of the class: on the connection of its receiver.
        template <void (*Function)(Framework::Scripting::Bind::Call &, uint64_t)>
        void OnReceiver(Framework::Scripting::Bind::Call &call) {
            if (const auto *self = call.Self<PendingConnection>()) {
                Function(call, self->GetGuid());
            }
        }

        // A call as a function of the JavaScript object of a connection.
        template <void (*Function)(Framework::Scripting::Bind::Call &, uint64_t)>
        v8::Local<v8::Value> OnConnection(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid) {
            return Framework::Scripting::Bind::MakeV8Function(isolate, context, [guid](Framework::Scripting::Bind::Call &call) {
                Function(call, guid);
            });
        }

        void DefineValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, const char *name, v8::Local<v8::Value> value) {
            const auto attributes = static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
            target->DefineOwnProperty(context, v8pp::to_v8(isolate, name), value, attributes).Check();
        }

        // Where the JavaScript object of a connection keeps what it crosses as (Encode), in an
        // ArrayBuffer: what tells the transfer that an object is one, under a key no script can name.
        v8::Local<v8::Private> PayloadKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.pendingConnection.payload"));
        }

        // Where a context keeps the object every connection made in it is a copy of, on its global.
        v8::Local<v8::Private> BlankKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.pendingConnection.blank"));
        }

        // A new object for a connection: plain to a script, with one internal field it never sees. The
        // transfer asks its host types about native-backed objects only, and tells those by their
        // internal fields. The template is instantiated once for a context; every object after is a
        // copy of that instance.
        v8::Local<v8::Object> NewConnectionObject(v8::Isolate *isolate, v8::Local<v8::Context> context) {
            const v8::Local<v8::Object> global = context->Global();
            v8::Local<v8::Value> blank;
            if (!global->GetPrivate(context, BlankKey(isolate)).ToLocal(&blank) || !blank->IsObject()) {
                v8::Local<v8::ObjectTemplate> shape = v8::ObjectTemplate::New(isolate);
                shape->SetInternalFieldCount(1);
                const v8::Local<v8::Object> made = shape->NewInstance(context).ToLocalChecked();
                // An instance of a template has a prototype of its own; a plain object has the one of every object.
                made->SetPrototypeV2(context, v8::Object::New(isolate)->GetPrototypeV2()).Check();
                global->SetPrivate(context, BlankKey(isolate), made).Check();
                blank = made;
            }
            return blank.As<v8::Object>()->Clone();
        }

        void WriteBytes(std::vector<uint8_t> &bytes, const void *data, size_t size) {
            const auto *from = static_cast<const uint8_t *>(data);
            bytes.insert(bytes.end(), from, from + size);
        }

        // Reads what is there, and zeroes for what is not.
        void ReadBytes(const std::vector<uint8_t> &bytes, size_t &at, void *into, size_t size) {
            std::memset(into, 0, size);
            if (at >= bytes.size()) {
                return;
            }
            const size_t taken = std::min(size, bytes.size() - at);
            std::memcpy(into, bytes.data() + at, taken);
            at += taken;
        }
    } // namespace

    PendingConnection::PendingConnection(uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, std::string address): _guid(guid), _identity(identity), _address(std::move(address)) {}

    void PendingConnection::Encode(std::vector<uint8_t> &bytes) const {
        bytes.clear();
        WriteBytes(bytes, &_guid, sizeof(_guid));
        for (const std::string *text : {&_identity.name, &_identity.steamId, &_identity.epicId, &_identity.discordId, &_identity.discordName, &_identity.hardwareId, &_identity.ticket, &_address}) {
            const uint32_t length = static_cast<uint32_t>(text->size());
            WriteBytes(bytes, &length, sizeof(length));
            WriteBytes(bytes, text->data(), text->size());
        }
    }

    PendingConnection PendingConnection::Decode(const std::vector<uint8_t> &bytes) {
        size_t at     = 0;
        uint64_t guid = 0;
        ReadBytes(bytes, at, &guid, sizeof(guid));

        Framework::Networking::RPC::ClientIdentity identity;
        std::string address;
        for (std::string *text : {&identity.name, &identity.steamId, &identity.epicId, &identity.discordId, &identity.discordName, &identity.hardwareId, &identity.ticket, &address}) {
            uint32_t length = 0;
            ReadBytes(bytes, at, &length, sizeof(length));
            // Bytes that are cut short read as empty text.
            const size_t taken = std::min<size_t>(length, bytes.size() - at);
            if (taken > 0) {
                text->assign(reinterpret_cast<const char *>(bytes.data()) + at, taken);
                at += taken;
            }
        }
        return PendingConnection(guid, identity, std::move(address));
    }

    const Framework::Scripting::Bind::Class<PendingConnection> &PendingConnection::Definition() {
        static const auto definition =
            Framework::Scripting::Bind::Class<PendingConnection>("PendingConnection", "A player asking to join, handed to `playerConnecting`. Every identifier is reported by the player's own client and is not verified by the server.", Framework::Scripting::Bind::Side::Server)
                // Handed to the handlers of `playerConnecting` and never reached by name.
                .ValueOnly()
                .Property("nickname", &PendingConnection::GetNickname, "The name the player asked to join under.", Framework::Scripting::Bind::Side::Server)
                .Property("steamId", &PendingConnection::GetSteamId, "Steam identifier the client reported, or an empty string when it had none.", Framework::Scripting::Bind::Side::Server)
                .Property("epicId", &PendingConnection::GetEpicId, "Epic Games account identifier the client reported, or an empty string when it had none.", Framework::Scripting::Bind::Side::Server)
                .Property("discordId", &PendingConnection::GetDiscordId, "Discord identifier the client reported, or an empty string when it had none.", Framework::Scripting::Bind::Side::Server)
                .Property("discordName", &PendingConnection::GetDiscordName, "Discord username the client reported, or an empty string when it had none.", Framework::Scripting::Bind::Side::Server)
                .Property("hardwareId", &PendingConnection::GetHardwareId, "Framework hardware identifier the client reported, or an empty string when it had none.", Framework::Scripting::Bind::Side::Server)
                .Property("ticket", &PendingConnection::GetTicket,
                    "The string the client was launched with, as the `ticket` of its launch link, or an empty string. The framework passes it through untouched; checking it -- against a one-time join ticket your own launcher or website issued, typically -- is "
                    "the script's job.",
                    Framework::Scripting::Bind::Side::Server)
                .Property("ip", &PendingConnection::GetIp, "The remote address the connection comes from, without its port.", Framework::Scripting::Bind::Side::Server)
                // Context functions: a reason may be left out, and each says in its own words what it was given instead.
                .Method("reject", &OnReceiver<&Reject>,
                    Framework::Scripting::Bind::Doc("Turns the player away before they receive anything. Takes effect on the next server tick, whatever other handlers are still doing. Does nothing once the connection is decided or the player has left.")
                        .OptionalParam("reason", Framework::Scripting::Bind::TypeOf<std::string>(), "Shown to the player as written, up to 512 bytes. Omitted, they read that the server refused the connection."),
                    Framework::Scripting::Bind::Side::Server)
                .Method("update", &OnReceiver<&Update>,
                    Framework::Scripting::Bind::Doc("Shows the waiting player a line of status and restarts the admission timeout, so a queue that keeps updating its players is never timed out. Does nothing once the connection is decided or the player has left.")
                        .Param("message", Framework::Scripting::Bind::TypeOf<std::string>(), "One line for the player's connecting screen, up to 256 bytes: 'Checking the whitelist', 'You are 4th in the queue'."),
                    Framework::Scripting::Bind::Side::Server)
                .Method("isPending", &OnReceiver<&IsPending>, Framework::Scripting::Bind::Doc("Checks whether this connection is still waiting on a decision.").ReturnType(Framework::Scripting::Bind::TypeOf<bool>()).Returns("False once it was let in or turned away, or the player gave up and left; a queue drops such entries."),
                    Framework::Scripting::Bind::Side::Server)
                // Crosses between runtimes with its identity, so that one the gate has decided still arrives.
                .Transfer(&PendingConnection::Encode, &PendingConnection::Decode);
        return definition;
    }

    v8::Local<v8::Object> PendingConnection::ToObject(v8::Isolate *isolate, v8::Local<v8::Context> context) const {
        v8::EscapableHandleScope handleScope(isolate);
        // The members the definition declares, as properties of the object's own, in its order.
        v8::Local<v8::Object> connection = NewConnectionObject(isolate, context);

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

        std::vector<uint8_t> payload;
        Encode(payload);
        const v8::Local<v8::ArrayBuffer> carried = v8::ArrayBuffer::New(isolate, payload.size());
        std::memcpy(carried->Data(), payload.data(), payload.size());
        connection->SetPrivate(context, PayloadKey(isolate), carried).Check();

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

        // Both languages, as the definition declares it: that is how a Lua handle crosses.
        Definition().RegisterTransfer();

        // JavaScript has no object of the class: a connection is a plain object there, told by the
        // payload it carries, and built again as one. Registered under the same name, this replaces
        // the JavaScript half the definition registered, so it has to come after it.
        Framework::Scripting::ValueTransfer::RegisterHostType(
            Definition().Describe().name,
            [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                v8::Local<v8::Value> carried;
                if (!object->GetPrivate(isolate->GetCurrentContext(), PayloadKey(isolate)).ToLocal(&carried) || !carried->IsArrayBuffer()) {
                    return false;
                }
                const v8::Local<v8::ArrayBuffer> payload = carried.As<v8::ArrayBuffer>();
                const auto *data                         = static_cast<const uint8_t *>(payload->Data());
                bytes.assign(data, data + payload->ByteLength());
                return true;
            },
            [](v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                return Decode(bytes).ToObject(isolate, context);
            });
    }

    void PendingConnection::Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages) {
        registrar.Type(Definition(), languages);
        if (!registrar.HasJavaScript(languages)) {
            return;
        }

        auto &catalog = Framework::Scripting::GetScriptingCatalog(registrar.Isolate());

        catalog.data_type("EventMap")
            .add_property(ConnectionGate::kEventName, "[connection: PendingConnection]",
                "Dispatched when a player asks to join, before the connection exists: they hold no player slot, are not counted as online, and are sent nothing -- no resource list, no download, no body. The request waits until every "
                "handler has returned and every Promise a handler returned has settled, then it is let in if a player slot is free (otherwise it is refused as full). `connection.reject()` turns it away instead, and so does a handler "
                "that throws or rejects, or handlers that have not settled within the server's admission timeout (30 seconds by default, restarted by every `connection.update()`). With no handler at all, every request is let in at once.");

        // The type is described the way the definition declares it. It is recorded as a data type and
        // not as a class: JavaScript has no constructor of it, only the objects the gate hands out.
        Framework::Scripting::Bind::RecordV8DataType(catalog, Definition().Describe());
    }

} // namespace Framework::Integrations::Server::Scripting::Builtins
