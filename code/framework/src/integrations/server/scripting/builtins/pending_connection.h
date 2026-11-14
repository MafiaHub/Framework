/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/client_identity.h>
#include <scripting/bind/class.h>
#include <scripting/bind/registrar.h>

#include <v8.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Integrations::Server::Scripting::Builtins {
    class PendingConnection;
} // namespace Framework::Integrations::Server::Scripting::Builtins

FW_SCRIPT_CLASS(Framework::Integrations::Server::Scripting::Builtins::PendingConnection, "PendingConnection");

namespace Framework::Integrations::Server::Scripting::Builtins {

    // The connection handed to playerConnecting handlers: who is asking to join, and the calls
    // that answer them. Not a Player: there is no body, no entity and no network id yet, and
    // there may never be one.
    //
    // Declared once for both languages. To JavaScript it is the plain object it always was, with
    // its values and its three functions as read-only properties of its own; to Lua it is a
    // handle of the class, with the same members.
    //
    // Its methods reach the ConnectionGate through CoreModules and address the connection by its
    // guid, so an object kept past its decision (or its player leaving) simply stops doing
    // anything, as a stale handle does everywhere else.
    class PendingConnection final {
      public:
        PendingConnection(uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, std::string address);

        // What a connection crosses between runtimes as: its guid, its identity and its address, so that it
        // is built again without asking the gate, which may have decided it by then. Such a handle reads
        // as it did, is no longer pending, and its calls do nothing.
        void Encode(std::vector<uint8_t> &bytes) const;
        static PendingConnection Decode(const std::vector<uint8_t> &bytes);

        uint64_t GetGuid() const {
            return _guid;
        }
        std::string GetNickname() const {
            return _identity.name;
        }
        std::string GetSteamId() const {
            return _identity.steamId;
        }
        std::string GetEpicId() const {
            return _identity.epicId;
        }
        std::string GetDiscordId() const {
            return _identity.discordId;
        }
        std::string GetDiscordName() const {
            return _identity.discordName;
        }
        std::string GetHardwareId() const {
            return _identity.hardwareId;
        }
        std::string GetTicket() const {
            return _identity.ticket;
        }
        std::string GetIp() const {
            return _address;
        }

        // The type as scripts see it, declared once for both languages.
        static const Framework::Scripting::Bind::Class<PendingConnection> &Definition();

        // Makes the type known in the languages of `languages`, for the handles the handlers receive; installs no
        // global. JavaScript gets the type and the playerConnecting event in its catalog.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All);

        // Lets a connection cross between runtimes as itself, in both languages. Process-wide and
        // idempotent; the server scripting module's constructor sees to it.
        static void RegisterTransfer();

        static v8::Local<v8::Object> Create(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, const std::string &address);

      private:
        // The JavaScript object of this connection.
        v8::Local<v8::Object> ToObject(v8::Isolate *isolate, v8::Local<v8::Context> context) const;

        uint64_t _guid;
        Framework::Networking::RPC::ClientIdentity _identity;
        std::string _address;
    };

} // namespace Framework::Integrations::Server::Scripting::Builtins
