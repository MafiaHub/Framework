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

    void Chat::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> frameworkObj, Framework::Scripting::ResourceManager *resourceManager) {
        (void)frameworkObj;
        (void)resourceManager;
        if (!isolate || context.IsEmpty()) {
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
        context->Global()->Set(context, v8pp::to_v8(isolate, "Chat"), chatObj).Check();
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
