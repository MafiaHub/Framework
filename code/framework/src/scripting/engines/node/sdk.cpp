/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

#include <string>

#include "builtins/quaternion.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"

#include "v8_helpers/v8_string.h"

#include "resource.h"

namespace Framework::Scripting::Engines::Node {
    static void On(const v8::FunctionCallbackInfo<v8::Value> &info) {
        // Ensure that the method was called with exactly two arguments
        if (info.Length() != 2) {
            v8::Isolate* isolate = info.GetIsolate();
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Wrong number of arguments").ToLocalChecked()));
            return;
        }

        // Ensure that the first argument is a string and the second is a function
        if (!info[0]->IsString() || !info[1]->IsFunction()) {
            // Throw an error if the argument types are incorrect
            v8::Isolate* isolate = info.GetIsolate();
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Invalid argument types: must be string and function").ToLocalChecked()));
            return;
        }

        const auto isolate = info.GetIsolate();
        const auto ctx     = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::String> eventName       = info[0]->ToString(ctx).ToLocalChecked();
        v8::Local<v8::Function> eventCallback = info[1].As<v8::Function>();

        // Create a persistent handle to the event callback function
        v8::Persistent<v8::Function> persistentCallback(isolate, eventCallback);

        const auto resource = static_cast<Node::Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));
        resource->_eventHandlers[Helpers::ToString(eventName, isolate)].push_back(persistentCallback);
    }

    bool SDK::Init(v8::Isolate *isolate, SDKRegisterCallback cb) {
        _module = new v8pp::module(isolate);
        _isolate = isolate;

        // Bind the module handler
        _module->function("on", &On);

        // Bind the builtins
        Builtins::Quaternion::Register(isolate, _module);
        Builtins::Vector3::Register(isolate, _module);
        Builtins::Vector2::Register(isolate, _module);

        // Always bind the mod-side in last
        if(cb){
            cb(Framework::Scripting::Engines::SDKRegisterWrapper(this, ENGINE_NODE));
        }

        return true;
    }
} // namespace Framework::Scripting::Engines::Node
