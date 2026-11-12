/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "chat.h"

#include <scripting/builtins/script_args.h>

#include "integrations/client/instance.h"
#include "integrations/client/ui/chat_box.h"

#include "core_modules.h"

#include <scripting/lua/lua_runtime.h>
#include <scripting/scripting_catalog.h>

#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        UI::ChatBox *ResolveBox() {
            auto *instance = CoreModules::GetClientInstance();
            return instance ? &instance->GetChatBox() : nullptr;
        }

        void Send(Framework::Scripting::Bind::Call &call) {
            if (!call.Arg(0).IsString()) {
                call.ThrowError("Chat.send: expected (text)");
                return;
            }
            if (auto *instance = CoreModules::GetClientInstance()) {
                instance->SendChatMessage(call.Arg(0).AsString());
            }
        }

        void SetUIVisible(Framework::Scripting::Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowError("Chat.setUIVisible: expected (visible)");
                return;
            }
            if (auto *box = ResolveBox()) {
                box->SetVisible(Framework::Scripting::Builtins::ReadFlag(call, 0));
            }
        }

        void IsUIVisible(Framework::Scripting::Bind::Call &call) {
            auto *box = ResolveBox();
            call.Return(box && box->IsVisible());
        }

        void Open(Framework::Scripting::Bind::Call &) {
            if (auto *box = ResolveBox()) {
                box->OpenInput();
            }
        }

        void Close(Framework::Scripting::Bind::Call &) {
            if (auto *box = ResolveBox()) {
                box->CloseInput(false);
            }
        }

        void IsOpen(Framework::Scripting::Bind::Call &call) {
            auto *box = ResolveBox();
            call.Return(box && box->IsInputActive());
        }
    } // namespace

    const Framework::Scripting::Bind::Namespace &Chat::Definition() {
        static const auto definition = [] {
            const auto boolean = Framework::Scripting::Bind::TypeOf<bool>();
            return Framework::Scripting::Bind::Namespace("Chat", "Client chat transport and native chat-box controls.", Framework::Scripting::Bind::Side::Client)
                .Function("send", &Send, Framework::Scripting::Bind::Doc("Sends a chat line to the server, bypassing the chatSend event; incoming lines arrive through the reserved chatMessage event.").Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Player-authored chat text sent to the server."), Framework::Scripting::Bind::Side::Client)
                .Function("setUIVisible", &SetUIVisible,
                    Framework::Scripting::Bind::Doc("Changes visibility of the native chat overlay without opening its input field. Hidden, the overlay also stops opening on its key. The choice holds for the rest of the session and resets to visible on disconnect, so a resource that replaces the chat calls this once at startup.")
                        .Param("visible", boolean, "Whether the chat overlay is rendered."),
                    Framework::Scripting::Bind::Side::Client)
                .Function("isUIVisible", &IsUIVisible, Framework::Scripting::Bind::Doc("Checks whether the native chat overlay is visible.").ReturnType(boolean).Returns("True when the overlay is currently rendered."), Framework::Scripting::Bind::Side::Client)
                .Function("open", &Open, Framework::Scripting::Bind::Doc("Opens and focuses the native chat input field."), Framework::Scripting::Bind::Side::Client)
                .Function("close", &Close, Framework::Scripting::Bind::Doc("Closes the native chat input field without submitting its contents."), Framework::Scripting::Bind::Side::Client)
                .Function("isOpen", &IsOpen, Framework::Scripting::Bind::Doc("Checks whether the native chat input field is active.").ReturnType(boolean).Returns("True while chat is capturing keyboard input."), Framework::Scripting::Bind::Side::Client);
        }();
        return definition;
    }

    void Chat::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }
        if (resourceManager != nullptr) {
            Framework::Scripting::Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, target, Framework::Scripting::GetScriptingCatalog(isolate), Framework::Scripting::Bind::Side::Client);

        auto &events = Framework::Scripting::GetScriptingCatalog(isolate).data_type("EventMap");
        events.add_property("chatMessage", "[message: { author: string; text: string; color: number }]", "Dispatched for every chat line the server sends. An empty author is a notice line; color is packed 0xRRGGBBAA, 0 for the chat box's default.");
        events.add_property("chatSend", "[text: string]", "Dispatched synchronously for a line the player typed into the chat box, before it is sent. A handler returning false, not a Promise of it, blocks it; Chat.send does not raise this.");
    }

    void Chat::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
