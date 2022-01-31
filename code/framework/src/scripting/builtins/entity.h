/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../keys.h"
#include "../resource.h"
#include "../engine.h"
#include "../v8_helpers/helpers.h"
#include "../v8_helpers/v8_class.h"
#include "../v8_helpers/v8_module.h"
#include "factory.h"

#include "logging/logger.h"

#include "world/engine.h"

#include "world/modules/base.hpp"

#include <sstream>

#include <flecs/flecs.h>

namespace Framework::Scripting::Builtins {
    inline void EntityGetID(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, flecs::entity &handle) {
        uint64_t id;
        V8Helpers::SafeToInteger(V8Helpers::Get(ctx, obj, "id"), ctx, id);

        V8_GET_RESOURCE();

        handle = resource->GetEngine()->GetWorldEngine()->WrapEntity(id);
    }

    inline bool EntityInvalid(flecs::entity ent) {
        if (!ent.is_alive()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Entity {} is invalid", ent.id());
            return true;
        }
        return false;
    }

    static void EntityConstructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_VALIDATE_CTOR_CALL();

        V8_GET_SELF();

        V8_GET_RESOURCE();

        flecs::entity ent;

        if (info.Length() > 0) {
            auto name = Helpers::ToString(info[0]->ToString(ctx).ToLocalChecked());
            ent = resource->GetEngine()->GetWorldEngine()->CreateEntity(name);
        } else {
            ent = resource->GetEngine()->GetWorldEngine()->CreateEntity();
        }

        V8_DEF_PROP("id", v8::BigInt::NewFromUnsigned(isolate, ent.id()));
    }

    static void EntityAlive(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        V8_RETURN(v8::Boolean::New(isolate, ent.is_alive()));
    }

    static void EntityDestroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        if (EntityInvalid(ent)) {
            return;
        }

        V8_GET_RESOURCE();

        resource->GetEngine()->GetWorldEngine()->RemoveEntity(ent);

        V8_RETURN_NULL();
    }

    static void EntityToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        std::ostringstream ss;
        ss  << "Entity{ id: " << ent.id() << ", baseId: " << ent.base_id() << ", alive: " << ent.is_alive() << " }";
        V8_RETURN(v8::String::NewFromUtf8(isolate, (ss.str().c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void EntityRegister(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto entClass = new Helpers::V8Class(
            GetKeyName(Keys::KEY_ENTITY), EntityConstructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();

                V8Helpers::SetMethod(isolate, tpl, "toString", EntityToString);
                V8Helpers::SetMethod(isolate, tpl, "alive", EntityAlive);
                V8Helpers::SetMethod(isolate, tpl, "destroy", EntityDestroy);
            });

        rootModule->AddClass(entClass);
    }
};
