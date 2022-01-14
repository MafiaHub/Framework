/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../errors.h"
#include "../resource.h"
#include "../v8_helpers/v8_string.h"

#include <v8.h>

namespace Framework::Scripting::Builtins {
    static void OnEvent(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (info.Length() == 2) {
            if (!info[0]->IsString() || !info[1]->IsFunction()) {
                return;
            }
            auto isolate = info.GetIsolate();
            if (!isolate) {
                return;
            }
            auto context = isolate->GetEnteredOrMicrotaskContext();
            if (context.IsEmpty()) {
                return;
            }
            auto resource = static_cast<Framework::Scripting::Resource *>(context->GetAlignedPointerFromEmbedderData(0));
            if (!resource) {
                return;
            }

            v8::Local<v8::String> eventName       = info[0]->ToString(context).ToLocalChecked();
            v8::Local<v8::Function> eventCallback = info[1].As<v8::Function>();
            resource->SubscribeEvent(Helpers::ToCString(eventName), eventCallback, Helpers::SourceLocation::GetCurrent(isolate));
            return;
        }
    }
} // namespace Framework::Scripting::Builtins
