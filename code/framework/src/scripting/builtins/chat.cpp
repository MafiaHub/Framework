/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "chat.h"

#include "../scripting_catalog.h"
#include "entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/rpc/chat_message.h>

#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <mafianet/PacketPriority.h>
#include <mafianet/types.h>

#include <cstdint>
#include <string>

namespace Framework::Scripting::Builtins {
    namespace {
        // Pull the optional { author, color } out of an options object argument.
        void ReadOptions(v8::Isolate *isolate, v8::Local<v8::Value> value, std::string &author, uint32_t &color) {
            if (value.IsEmpty() || !value->IsObject()) {
                return;
            }
            auto ctx                   = isolate->GetCurrentContext();
            v8::Local<v8::Object> opts = value.As<v8::Object>();
            v8::Local<v8::Value> a;
            if (opts->Get(ctx, v8pp::to_v8(isolate, "author")).ToLocal(&a) && a->IsString()) {
                author = v8pp::from_v8<std::string>(isolate, a);
            }
            v8::Local<v8::Value> c;
            if (opts->Get(ctx, v8pp::to_v8(isolate, "color")).ToLocal(&c) && c->IsNumber()) {
                color = c->Uint32Value(ctx).FromMaybe(0u);
            }
        }

        void Send(const std::string &text, const std::string &author, uint32_t color, MafiaNet::RakNetGUID target, bool broadcast) {
            auto *net = CoreModules::GetNetworkPeer();
            if (!net) {
                return;
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
        }
    } // namespace

    void Chat::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (!isolate || global.IsEmpty()) {
            return;
        }
        auto ctx = isolate->GetCurrentContext();

        v8::Local<v8::Object> chatObj = v8::Object::New(isolate);
        const auto attach             = [&](const char *name, v8::FunctionCallback cb) {
            chatObj->Set(ctx, v8pp::to_v8(isolate, name), v8::Function::New(ctx, cb).ToLocalChecked()).Check();
        };
        attach("sendToAll", &Chat::JS_SendToAll);
        attach("sendToPlayer", &Chat::JS_SendToPlayer);
        global->Set(ctx, v8pp::to_v8(isolate, "Chat"), chatObj).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("Chat", "Server-side delivery of structured chat messages to connected clients.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("sendToAll", v8pp::metadata::docs("void",
                                                                                           {
                                                                                               v8pp::metadata::param("text", "string", false, "Message body broadcast to every connected client."),
                                                                                               v8pp::metadata::param("options", "{ author?: string; color?: number }", true, "Optional author label and packed 0xRRGGBBAA color."),
                                                                                           },
                                                                                           "Broadcasts a structured chat message to all clients.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("sendToPlayer", v8pp::metadata::docs("void",
                                                                                              {
                                                                                                  v8pp::metadata::param("player", "Entity", false, "Player-owned entity identifying the destination connection."),
                                                                                                  v8pp::metadata::param("text", "string", false, "Message body sent to the player."),
                                                                                                  v8pp::metadata::param("options", "{ author?: string; color?: number }", true, "Optional author label and packed 0xRRGGBBAA color."),
                                                                                              },
                                                                                              "Sends a structured chat message to one player's owning connection.")));
    }

    void Chat::JS_SendToAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);
        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Chat.sendToAll: expected (text, opts?)"));
            return;
        }
        std::string author;
        uint32_t color = 0;
        if (info.Length() >= 2) {
            ReadOptions(isolate, info[1], author, color);
        }
        Send(v8pp::from_v8<std::string>(isolate, info[0]), author, color, MafiaNet::UNASSIGNED_RAKNET_GUID, true);
    }

    void Chat::JS_SendToPlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope hs(isolate);
        if (info.Length() < 2 || !info[1]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Chat.sendToPlayer: expected (player, text, opts?)"));
            return;
        }
        auto *entity = v8pp::class_<Entity>::unwrap_object(isolate, info[0]);
        if (!entity) {
            return;
        }
        auto *handle = entity->GetHandle();
        if (!handle) {
            return;
        }
        std::string author;
        uint32_t color = 0;
        if (info.Length() >= 3) {
            ReadOptions(isolate, info[2], author, color);
        }
        Send(v8pp::from_v8<std::string>(isolate, info[1]), author, color, MafiaNet::ToGuid(handle->ownerGUID), false);
    }
} // namespace Framework::Scripting::Builtins
