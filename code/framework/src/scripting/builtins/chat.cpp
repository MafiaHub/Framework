/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "chat.h"

#include "../scripting_catalog.h"
#include "color.h"
#include "entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/rpc/chat_message.h>

#include <mafianet/packet_priority.h>
#include <mafianet/types.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace Framework::Scripting::Builtins {
    namespace {
        // ToUint32 of JavaScript, which is how a packed color was always read: the whole part, wrapped to 32 bits.
        uint32_t ToUint32(double number) {
            if (!std::isfinite(number)) {
                return 0;
            }
            const double wrapped = std::fmod(std::trunc(number), 4294967296.0);
            return static_cast<uint32_t>(static_cast<int64_t>(wrapped));
        }

        // Pull the optional { author, color } out of an options argument. Anything that is not an object has
        // neither, and a member of another type is left out: nothing here is an error.
        void ReadOptions(const Bind::Value &options, std::string &author, uint32_t &color) {
            const Bind::Value name = options.Member("author");
            if (name.IsString()) {
                author = name.AsString();
            }
            const Bind::Value tint = options.Member("color");
            if (tint.IsNumber()) {
                color = ToUint32(tint.AsNumber());
            }
            else if (const auto *col = tint.AsBound<Color>()) {
                // Pack a Color into Chat's 0xRRGGBBAA layout.
                color = col->toRGBA();
            }
        }

        // Returns false when the network peer is unavailable so the caller can throw a state error.
        bool Send(const std::string &text, const std::string &author, uint32_t color, MafiaNet::RakNetGUID target, bool broadcast) {
            auto *net = CoreModules::GetNetworkPeer();
            if (!net) {
                return false;
            }
            Networking::RPC::ChatMessage payload;
            payload.text   = text;
            payload.author = author;
            payload.color  = color;
            if (broadcast) {
                net->BroadcastRPC(payload);
            }
            else {
                net->SendRPC(payload, target);
            }
            return true;
        }

        void SendToAll(Bind::Call &call) {
            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Chat.sendToAll: expected (text, opts?)");
                return;
            }
            std::string author;
            uint32_t color = 0;
            ReadOptions(call.Arg(1), author, color);
            if (!Send(call.Arg(0).AsString(), author, color, MafiaNet::UNASSIGNED_RAKNET_GUID, true)) {
                call.ThrowError("Chat.sendToAll: network peer unavailable");
            }
        }

        void SendToPlayer(Bind::Call &call) {
            if (!call.Arg(1).IsString()) {
                call.ThrowTypeError("Chat.sendToPlayer: expected (player, text, opts?)");
                return;
            }
            // Bad argument (not a player) is the caller's bug — throw, mirroring Entity.setVisibleTo.
            const auto *entity = call.Arg(0).AsBound<Entity>();
            if (!entity) {
                call.ThrowTypeError("Chat.sendToPlayer: expected a Player");
                return;
            }
            auto *handle = entity->GetHandle();
            if (!handle) {
                call.ThrowError("Chat.sendToPlayer: player has no owning connection");
                return;
            }
            std::string author;
            uint32_t color = 0;
            ReadOptions(call.Arg(2), author, color);
            if (!Send(call.Arg(1).AsString(), author, color, MafiaNet::ToGuid(handle->ownerGUID), false)) {
                call.ThrowError("Chat.sendToPlayer: network peer unavailable");
            }
        }
    } // namespace

    const Bind::Namespace &Chat::Definition() {
        static const auto definition = [] {
            const auto options      = Bind::Type::Custom("{ author?: string; color?: number | Color }", "{ author: string?, color: (integer|Color)? }");
            const char *optionsHelp = "Optional author label and color: a Color or a packed 0xRRGGBBAA integer.";
            return Bind::Namespace("Chat", "Server-side delivery of structured chat messages to connected clients.", Framework::Scripting::Bind::Side::Server)
                .Function("sendToAll", &SendToAll, Framework::Scripting::Bind::Doc("Broadcasts a structured chat message to all clients.").Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Message body broadcast to every connected client.").OptionalParam("options", options, optionsHelp), Framework::Scripting::Bind::Side::Server)
                .Function("sendToPlayer", &SendToPlayer,
                    Framework::Scripting::Bind::Doc("Sends a structured chat message to one player's owning connection.")
                        .Param("player", Framework::Scripting::Bind::TypeOf<Entity>(), "Player-owned entity identifying the destination connection.")
                        .Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Message body sent to the player.")
                        .OptionalParam("options", options, optionsHelp),
                    Framework::Scripting::Bind::Side::Server);
        }();
        return definition;
    }

    void Chat::Register(Bind::Registrar &registrar, Bind::Languages languages) {
        registrar.Global(Definition(), languages);
    }
} // namespace Framework::Scripting::Builtins
