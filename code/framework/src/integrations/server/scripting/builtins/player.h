/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../server.h"
#include "scripting/builtins/factory.h"
#include "scripting/keys.h"
#include "scripting/resource.h"
#include "scripting/v8_helpers/helpers.h"
#include "scripting/v8_helpers/v8_class.h"
#include "scripting/v8_helpers/v8_module.h"

#include "logging/logger.h"

#include "world/engine.h"

#include "../helpers.h"

#include "world/modules/base.hpp"

#include "integrations/shared/types/streaming.hpp"

#include <sstream>

#include <flecs/flecs.h>

namespace Framework::Scripting::Builtins {
    static void PlayerConstructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_VALIDATE_CTOR_CALL();

        V8_GET_SELF();

        V8_GET_RESOURCE();

        flecs::entity ent;

        if (info.Length() > 0 && info[0]->IsBigInt()) {
            const auto id = info[0]->ToBigInt(ctx).ToLocalChecked()->Uint64Value();
            ent           = V8_IN_GET_WORLD()->WrapEntity(id);
        }
        else {
            Helpers::Throw(isolate, "Can't instantiate Player class!");
            V8_RETURN_NULL();
            return;
        }

        V8_DEF_PROP("id", v8::BigInt::NewFromUnsigned(isolate, ent.id()));
    }

    static void PlayerDestroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        Helpers::Throw(isolate, "Player.destroy() is not supported");
    }

    static void PlayerToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        const auto name = ent.is_alive() ? ent.name() : "invalid";

        const auto str = fmt::format("Player {{ id: {}, name: \"{}\", alive: {} }}", ent.id(), name ? name : "none", !EntityInvalid(isolate, ent, true));
        V8_RETURN(v8::String::NewFromUtf8(isolate, (str.c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void PlayerRegister(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto entClass = new Helpers::V8Class(
            GetKeyName(Keys::KEY_PLAYER), *rootModule->GetClass(GetKeyName(Keys::KEY_ENTITY)), PlayerConstructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();

                Helpers::SetMethod(isolate, tpl, "toString", PlayerToString);
                Helpers::SetMethod(isolate, tpl, "destroy", PlayerDestroy);
            });

        rootModule->AddClass(entClass);
    }
}; // namespace Framework::Scripting::Builtins
