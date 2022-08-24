/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

#include <v8.h>

#include <string>

#include "builtins/quaternion.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"

#include "v8_helpers/v8_string.h"

#include "resource.h"

namespace Framework::Scripting::Engines::Node {
    static void On(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (info.Length() == 2) {
            if (!info[0]->IsString() || !info[1]->IsFunction()) {
                return;
            }

            const auto isolate = info.GetIsolate();
            const auto ctx     = isolate->GetEnteredOrMicrotaskContext();

            v8::Local<v8::String> eventName       = info[0]->ToString(ctx).ToLocalChecked();
            v8::Local<v8::Function> eventCallback = info[1].As<v8::Function>();

            v8::Persistent<v8::Function> persistentCallback;
            persistentCallback.Reset(isolate, eventCallback);

            const auto resource = static_cast<Node::Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));
            resource->_eventHandlers[Helpers::ToString(eventName, isolate)].push_back(persistentCallback);
        }
    }

    bool SDK::Init(v8::Isolate *isolate) {
        _module = new v8pp::module(isolate);

        // Bind the module handler
        _module->function("on", &On);

        // Bind the builtins
        Builtins::QuaternionRegister(isolate, _module);
        Builtins::Vector3Register(isolate, _module);
        Builtins::Vector2Register(isolate, _module);

        // Always bind the mod-side in last
        //TODO: register callback
    }
} // namespace Framework::Scripting::Engines::Node