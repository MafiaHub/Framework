/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "chat.h"

#include "integrations/client/instance.h"
#include "integrations/client/ui/chat_box.h"

#include "core_modules.h"

#include <scripting/scripting_catalog.h>

#include <v8pp/convert.hpp>

#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        UI::ChatBox *ResolveBox() {
            auto *instance = CoreModules::GetClientInstance();
            return instance ? &instance->GetChatBox() : nullptr;
        }
    } // namespace

    void Chat::SendCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Chat.send: expected (text)")));
            return;
        }
        if (auto *instance = CoreModules::GetClientInstance()) {
            instance->SendChatMessage(v8pp::from_v8<std::string>(isolate, args[0]));
        }
    }

    void Chat::SetUIVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Chat.setUIVisible: expected (visible)")));
            return;
        }
        if (auto *box = ResolveBox()) {
            box->SetVisible(args[0]->BooleanValue(isolate));
        }
    }

    void Chat::IsUIVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *box = ResolveBox();
        args.GetReturnValue().Set(box && box->IsVisible());
    }

    void Chat::OpenCallback(const v8::FunctionCallbackInfo<v8::Value> &) {
        if (auto *box = ResolveBox()) {
            box->OpenInput();
        }
    }

    void Chat::CloseCallback(const v8::FunctionCallbackInfo<v8::Value> &) {
        if (auto *box = ResolveBox()) {
            box->CloseInput(false);
        }
    }

    void Chat::IsOpenCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *box = ResolveBox();
        args.GetReturnValue().Set(box && box->IsInputActive());
    }

    void Chat::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        (void)resourceManager;
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }

        const auto attach = [&](v8::Local<v8::Object> obj, const char *name, v8::FunctionCallback callback) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback);
            obj->Set(context, v8pp::to_v8(isolate, name), tmpl->GetFunction(context).ToLocalChecked()).Check();
        };

        v8::Local<v8::Object> chatObj = v8::Object::New(isolate);
        attach(chatObj, "send", &Chat::SendCallback);
        attach(chatObj, "setUIVisible", &Chat::SetUIVisibleCallback);
        attach(chatObj, "isUIVisible", &Chat::IsUIVisibleCallback);
        attach(chatObj, "open", &Chat::OpenCallback);
        attach(chatObj, "close", &Chat::CloseCallback);
        attach(chatObj, "isOpen", &Chat::IsOpenCallback);
        target->Set(context, v8pp::to_v8(isolate, "Chat"), chatObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Chat", "Client chat transport and native chat-box controls.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("send",
            v8pp::metadata::docs("void", {v8pp::metadata::param("text", "string", false, "Player-authored chat text sent to the server.")}, "Sends a chat line to the server, bypassing the chatSend event; incoming lines arrive through the reserved chatMessage event.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setUIVisible",
            v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "Whether the chat overlay is rendered.")}, "Changes visibility of the native chat overlay without opening its input field.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isUIVisible", v8pp::metadata::docs("boolean", {}, "Checks whether the native chat overlay is visible.", "True when the overlay is currently rendered.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("open", v8pp::metadata::docs("void", {}, "Opens and focuses the native chat input field.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("close", v8pp::metadata::docs("void", {}, "Closes the native chat input field without submitting its contents.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isOpen", v8pp::metadata::docs("boolean", {}, "Checks whether the native chat input field is active.", "True while chat is capturing keyboard input.")));
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
