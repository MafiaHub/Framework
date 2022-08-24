/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

#include "builtins/quaternion.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"

namespace Framework::Scripting::Engines::Node {
    bool SDK::Init(v8::Isolate *isolate) {
        _module = new v8pp::module(isolate);

        Builtins::QuaternionRegister(isolate, _module);
        Builtins::Vector3Register(isolate, _module);
        Builtins::Vector2Register(isolate, _module);
    }
} // namespace Framework::Scripting::Engines::Node