/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/rpc/chat_message.h>

#include <v8.h>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <mafianet/PacketPriority.h>
#include <mafianet/types.h>

#include <string>

namespace Framework::Scripting::Builtins {
    // Server-side chat API exposed to scripts as the global `Chat`. Reusable across mods: it sends
    // the framework ChatMessage RPC and targets players through the base Entity handle (a mod's own
    // Player/Human handle resolves to Entity via v8pp inheritance), so it needs no game-specific type.
    class Chat {
      public:
        static void SendToAll(std::string message) {
            Send(message, MafiaNet::UNASSIGNED_RAKNET_GUID, true);
        }

        // The argument is any replicated entity handle; the message is delivered to that entity's
        // owning connection (typically a player's avatar).
        static void SendToPlayer(Entity *entity, std::string message) {
            if (!entity) {
                return;
            }
            auto *handle = entity->GetHandle();
            if (!handle) {
                return;
            }
            Send(message, MafiaNet::RakNetGUID(handle->ownerGUID), false);
        }

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
            if (!isolate || global.IsEmpty()) {
                return;
            }
            v8pp::module chatModule(isolate);
            chatModule.function("sendToAll", &Chat::SendToAll);
            chatModule.function("sendToPlayer", &Chat::SendToPlayer);

            auto ctx = isolate->GetCurrentContext();
            global->Set(ctx, v8pp::to_v8(isolate, "Chat"), chatModule.new_instance()).Check();
        }

      private:
        static void Send(const std::string &message, MafiaNet::RakNetGUID target, bool broadcast) {
            auto *net = CoreModules::GetNetworkPeer();
            if (!net) {
                return;
            }
            Networking::RPC::ChatMessage payload {message};
            if (broadcast) {
                net->BroadcastRPC(payload);
            }
            else {
                net->SendRPC(payload, target);
            }
        }
    };
} // namespace Framework::Scripting::Builtins
