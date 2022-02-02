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

#define GET_ENTITY()                                                                                                                                                                                                                                                                   \
    V8_GET_ISOLATE_CONTEXT();                                                                                                                                                                                                                                                          \
    V8_GET_RESOURCE();                                                                                                                                                                                                                                                                 \
    V8_GET_SELF();                                                                                                                                                                                                                                                                     \
    flecs::entity ent;                                                                                                                                                                                                                                                                 \
    EntityGetID(ctx, _this, ent);                                                                                                                                                                                                                                                      \
    if (EntityInvalid(isolate, ent)) {                                                                                                                                                                                                                                                          \
        V8_RETURN_NULL();                                                                                                                                                                                                                                                              \
        return;                                                                                                                                                                                                                                                                        \
    }

namespace Framework::Scripting::Builtins {
    inline void EntityGetID(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, flecs::entity &handle) {
        uint64_t id;
        Helpers::SafeToInteger(Helpers::Get(ctx, obj, "id"), ctx, id);

        V8_GET_RESOURCE();

        handle = V8_IN_GET_WORLD()->WrapEntity(id);
    }

    inline bool EntityInvalid(v8::Isolate *isolate, flecs::entity ent, bool suppressLog = false) {
        if (!ent.is_alive() || (ent.is_alive() && ent.get<World::Modules::Base::PendingRemoval>())) {
            if (!suppressLog) {
                Helpers::Throw(isolate, fmt::format("Entity {} is invalid", ent.id()));
            }
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
        auto entCtor = Integrations::Shared::Archetypes::StreamingFactory();

        if (info.Length() > 0 && info[0]->IsBigInt()) {
            const auto id = info[0]->ToBigInt(ctx).ToLocalChecked()->Uint64Value();
            ent           = V8_IN_GET_WORLD()->WrapEntity(id);
        }
        else if (info.Length() > 0) {
            auto name = Helpers::ToString(info[0]->ToString(ctx).ToLocalChecked());
            ent       = V8_IN_GET_WORLD()->CreateEntity(name);
            entCtor.SetupServer(ent, 0);
        }
        else {
            ent = V8_IN_GET_WORLD()->CreateEntity();
            entCtor.SetupServer(ent, 0);
        }

        V8_DEF_PROP("id", v8::BigInt::NewFromUnsigned(isolate, ent.id()));
    }

    static void EntityAlive(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        V8_RETURN(v8::Boolean::New(isolate, !EntityInvalid(isolate, ent, true)));
    }

    static void EntityDestroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        if (EntityInvalid(isolate, ent)) {
            return;
        }

        // HACK - this is a hack to prevent the player from being destroyed if wrapped into sdk.Entity
        if (ent.get<World::Modules::Base::Streamer>()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Can't destroy player entity {}", ent.id());
            return;
        }

        V8_GET_RESOURCE();

        V8_IN_GET_WORLD()->RemoveEntity(ent);
    }

    static void EntityToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        flecs::entity ent;
        EntityGetID(ctx, _this, ent);

        const auto name = ent.is_alive() ? ent.name() : "invalid";

        const auto str = fmt::format("Entity {{ id: {}, name: \"{}\", alive: {} }}", ent.id(), name ? name : "none", !EntityInvalid(isolate, ent, true));
        V8_RETURN(v8::String::NewFromUtf8(isolate, (str.c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void EntityGetPos(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto tr = ent.get<World::Modules::Base::Transform>();

        if (!tr) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, tr->pos));
    }

    static void EntitySetPos(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto tr = ent.get_mut<World::Modules::Base::Transform>();

        if (!tr) {
            V8_RETURN_NULL();
            return;
        }

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        tr->pos = {newX, newY, newZ};
    }

    static void EntityGetRot(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto tr = ent.get<World::Modules::Base::Transform>();

        if (!tr) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, tr->rot));
    }

    static void EntitySetRot(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto tr = ent.get_mut<World::Modules::Base::Transform>();

        if (!tr) {
            V8_RETURN_NULL();
            return;
        }

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        tr->rot = glm::quat(static_cast<float>(newW), static_cast<float>(newX), static_cast<float>(newY), static_cast<float>(newZ));
    }

    static void EntityGetVelocity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto tr = ent.get<World::Modules::Base::Transform>();

        if (!tr) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, tr->vel));
    }

    static void EntitySetVelocity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto tr = ent.get_mut<World::Modules::Base::Transform>();

        if (!tr) {
            V8_RETURN_NULL();
            return;
        }

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        tr->vel = {newX, newY, newZ};
    }

    static void EntityGetFrameInfo(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto frame = ent.get<World::Modules::Base::Frame>();

        if (!frame) {
            V8_RETURN_NULL();
            return;
        }

        auto obj       = v8::Object::New(isolate);
        auto frameName = Helpers::MakeString(isolate, frame->modelName.c_str()).ToLocalChecked();
        auto modelHash = v8::BigInt::NewFromUnsigned(isolate, frame->modelHash);
        Helpers::Set(ctx, isolate, obj, "modelName", frameName);
        Helpers::Set(ctx, isolate, obj, "modelHash", modelHash);

        V8_RETURN(obj);
    }

    static void EntityGetScale(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto frame = ent.get<World::Modules::Base::Frame>();

        if (!frame) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, frame->scale));
    }

    static void EntitySetScale(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto frame = ent.get_mut<World::Modules::Base::Frame>();

        if (!frame) {
            V8_RETURN_NULL();
            return;
        }

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        frame->scale = {newX, newY, newZ};
    }

    static void EntityGetVirtualWorld(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto st = ent.get<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(v8::Integer::New(isolate, st->virtualWorld));
    }

    static void EntitySetVirtualWorld(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto st = ent.get_mut<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        Helpers::SafeToInteger(info[0], ctx, st->virtualWorld);
    }

    static void EntityGetVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto st = ent.get<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(v8::Boolean::New(isolate, st->isVisible));
    }

    static void EntitySetVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto st = ent.get_mut<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        Helpers::SafeToBoolean(info[0], ctx, st->isVisible);
    }

    static void EntityGetAlwaysVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto st = ent.get<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(v8::Boolean::New(isolate, st->alwaysVisible));
    }

    static void EntitySetAlwaysVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto st = ent.get_mut<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        Helpers::SafeToBoolean(info[0], ctx, st->alwaysVisible);
    }

    static void EntityGetUpdateRate(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto st = ent.get<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(v8::Number::New(isolate, st->updateInterval));
    }

    static void EntitySetUpdateRate(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto st = ent.get_mut<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        Helpers::SafeToNumber(info[0], ctx, st->updateInterval);
    }

    static void EntityGetOwnerID(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto st = ent.get_mut<World::Modules::Base::Streamable>();

        if (!st) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(v8::BigInt::NewFromUnsigned(isolate, st->owner));
    }

    static void EntityGetData(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        const auto customData = ent.get<Integrations::Shared::Modules::Mod::CustomData>();

        if (!customData) {
            Helpers::Throw(isolate, "Entity does not have custom data component");
            return;
        }

        if (!customData->data.Valid()) {
            V8_RETURN_NULL();
            return;
        }

        auto val = Helpers::Serialization::Deserialize(ctx, customData->data);
        if (val.IsEmpty()) {
            V8_RETURN_NULL();
            return;
        }

        V8_RETURN(val.ToLocalChecked());
    }

    static void EntitySetData(const v8::FunctionCallbackInfo<v8::Value> &info) {
        GET_ENTITY();

        auto customData = ent.get_mut<Integrations::Shared::Modules::Mod::CustomData>();

        if (!customData) {
            Helpers::Throw(isolate, "Entity does not have custom data component");
            return;
        }

        if (info.Length() == 0) {
            Helpers::Throw(isolate, "No data provided, call setData(null) to remove data explicitly");
            return;
        } else if (info.Length() > 1) {
            Helpers::Throw(isolate, "Too many arguments, expected 1");
            return;
        }

        Helpers::Serialization::Serialize(ctx, info[0], customData->data);
    }

    static void EntityRegister(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto entClass = new Helpers::V8Class(
            GetKeyName(Keys::KEY_ENTITY), EntityConstructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();

                Helpers::SetAccessor(isolate, tpl, "owner", EntityGetOwnerID);

                Helpers::SetMethod(isolate, tpl, "toString", EntityToString);
                Helpers::SetMethod(isolate, tpl, "isAlive", EntityAlive);
                Helpers::SetMethod(isolate, tpl, "destroy", EntityDestroy);

                Helpers::SetMethod(isolate, tpl, "getData", EntityGetData);
                Helpers::SetMethod(isolate, tpl, "setData", EntitySetData);

                Helpers::SetMethod(isolate, tpl, "getPosition", EntityGetPos);
                Helpers::SetMethod(isolate, tpl, "setPosition", EntitySetPos);

                Helpers::SetMethod(isolate, tpl, "getVelocity", EntityGetVelocity);
                Helpers::SetMethod(isolate, tpl, "setVelocity", EntitySetVelocity);

                Helpers::SetMethod(isolate, tpl, "getRotation", EntityGetRot);
                Helpers::SetMethod(isolate, tpl, "setRotation", EntitySetRot);

                Helpers::SetMethod(isolate, tpl, "getFrame", EntityGetFrameInfo);

                Helpers::SetMethod(isolate, tpl, "getScale", EntityGetScale);
                Helpers::SetMethod(isolate, tpl, "setScale", EntitySetScale);

                Helpers::SetMethod(isolate, tpl, "getVirtualWorld", EntityGetVirtualWorld);
                Helpers::SetMethod(isolate, tpl, "setVirtualWorld", EntitySetVirtualWorld);

                Helpers::SetMethod(isolate, tpl, "getVisible", EntityGetVisible);
                Helpers::SetMethod(isolate, tpl, "setVisible", EntitySetVisible);

                Helpers::SetMethod(isolate, tpl, "getAlwaysVisible", EntityGetAlwaysVisible);
                Helpers::SetMethod(isolate, tpl, "setAlwaysVisible", EntitySetAlwaysVisible);

                Helpers::SetMethod(isolate, tpl, "getUpdateRate", EntityGetUpdateRate);
                Helpers::SetMethod(isolate, tpl, "setUpdateRate", EntitySetUpdateRate);
            });

        rootModule->AddClass(entClass);
    }
}; // namespace Framework::Scripting::Builtins
