/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

#include "scripting/engines/node/builtins/core.h"
#include "scripting/engines/node/builtins/quaternion.h"
#include "scripting/engines/node/builtins/vector_2.h"
#include "scripting/engines/node/builtins/vector_3.h"
#include "logging/logger.h"

namespace Framework::Scripting {
    SDK::SDK(SDKRegisterCallback cb): _rootModule(nullptr) {
        _rootModule = new Helpers::V8Module("sdk", [](v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj) {
            Helpers::RegisterFunc(ctx, obj, "on", &Builtins::OnEvent);
        });

        if (!_rootModule) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Could not initialise the scripting sdk");
            return;
        }

        // Before inserting the reference, we create the required builtins
        RegisterBuiltins();

        // Call the SDK register callback for mod-provided implementations
        if (cb) {
            cb(this);
        }

        _modules.insert(_rootModule);
    }

    void SDK::RegisterBuiltins() {
        Builtins::Vector2Register(_rootModule);
        Builtins::Vector3Register(_rootModule);
        Builtins::QuaternionRegister(_rootModule);
    }

    bool SDK::RegisterModules(v8::Local<v8::Context> context) {
        for (auto mod : _modules) { mod->Register(context); }
        return true;
    }
} // namespace Framework::Scripting
