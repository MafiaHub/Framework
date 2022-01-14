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
#include "../v8_helpers/helpers.h"
#include "../v8_helpers/v8_class.h"
#include "../v8_helpers/v8_module.h"
#include "factory.h"

#include <glm/ext.hpp>
#include <glm/ext/matrix_relational.hpp>
#include <glm/ext/scalar_relational.hpp>
#include <glm/ext/vector_relational.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iomanip>
#include <sstream>

namespace Framework::Scripting::Builtins {
    inline void QuatExtract(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, double &w, double &x, double &y, double &z) {
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "w"), ctx, w);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "y"), ctx, y);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "z"), ctx, z);
    }

    static void QuaternionConstructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_VALIDATE_CTOR_CALL();

        V8_GET_SELF();
        V8_DEFINE_STACK();

        double w, x, y, z;
        if (!V8Helpers::GetQuat(ctx, stack, w, x, y, z)) {
            V8Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "w", v8::Number::New(isolate, w), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "x", v8::Number::New(isolate, x), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "y", v8::Number::New(isolate, y), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "z", v8::Number::New(isolate, z), v8::PropertyAttribute::ReadOnly);
    }

    static void QuaternionConj(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, glm::conjugate(oldQuat)));
    }

    static void QuaternionCross(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!V8Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        // Acquire old values
        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::quat newQuat(newW, newX, newY, newZ);
        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, glm::cross(oldQuat, newQuat)));
    }

    static void QuaternionDot(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!V8Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        // Acquire old values
        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::quat newQuat(newW, newX, newY, newZ);
        V8_RETURN(static_cast<float>(glm::dot(oldQuat, newQuat)));
    }

    static void QuaternionInverse(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, glm::inverse(oldQuat)));
    }

    static void QuaternionRotateVector3(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        V8_DEFINE_STACK();

        double pX, pY, pZ;
        if (!V8Helpers::GetVec3(ctx, stack, pX, pY, pZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::vec3 point(pX, pY, pZ);
        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldQuat * point));
    }

    static void QuaternionSlerp(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!V8Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        // Acquire factor
        double f;
        V8Helpers::SafeToNumber(info[4], ctx, f);

        // Acquire old values
        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::quat newQuat(newW, newX, newY, newZ);
        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, glm::mix(oldQuat, newQuat, static_cast<float>(f))));
    }

    static void QuaternionLength(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        glm::quat nativeQuat(w, x, y, z);
        V8_RETURN(static_cast<double>(glm::length(nativeQuat)));
    }

    static void QuaternionToArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 4);
        arr->Set(ctx, 0, v8::Number::New(isolate, w));
        arr->Set(ctx, 1, v8::Number::New(isolate, x));
        arr->Set(ctx, 2, v8::Number::New(isolate, y));
        arr->Set(ctx, 3, v8::Number::New(isolate, z));
        V8_RETURN(arr);
    }

    static void QuaternionToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << "Quaternion{ w: " << w << ", x: " << x << ", y: " << y << ", z: " << z << " }";
        V8_RETURN(v8::String::NewFromUtf8(isolate, (ss.str().c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void QuaternionFromEuler(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_DEFINE_STACK();

        double x, y, z;
        if (!V8Helpers::GetVec3(ctx, stack, x, y, z)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }
        glm::quat newQuat(glm::vec3(x, y, z));

        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, newQuat));
    }

    static void QuaternionFromAxisAngle(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        double angle, axisX, axisY, axisZ;
        V8Helpers::SafeToNumber(info[0], ctx, angle);
        V8Helpers::SafeToNumber(info[1], ctx, axisX);
        V8Helpers::SafeToNumber(info[2], ctx, axisY);
        V8Helpers::SafeToNumber(info[3], ctx, axisZ);

        glm::quat newQuat(glm::angleAxis(static_cast<float>(angle), glm::vec3(axisX, axisY, axisZ)));

        V8_RETURN(CreateQuaternion(resource->GetSDK()->GetRootModule(), ctx, newQuat));
    }

    static void QuaternionRegister(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto vec3Class = new Helpers::V8Class(
            GetKeyName(Keys::KEY_QUATERNION), QuaternionConstructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();
                V8Helpers::SetAccessor(isolate, tpl, "length", QuaternionLength);

                V8Helpers::SetStaticMethod(isolate, tpl, "fromEuler", QuaternionFromEuler);
                V8Helpers::SetStaticMethod(isolate, tpl, "fromAxisAngle", QuaternionFromAxisAngle);

                V8Helpers::SetMethod(isolate, tpl, "conj", QuaternionConj);
                V8Helpers::SetMethod(isolate, tpl, "dot", QuaternionDot);
                V8Helpers::SetMethod(isolate, tpl, "cross", QuaternionCross);
                V8Helpers::SetMethod(isolate, tpl, "inverse", QuaternionInverse);
                V8Helpers::SetMethod(isolate, tpl, "toArray", QuaternionToArray);
                V8Helpers::SetMethod(isolate, tpl, "toString", QuaternionToString);
                V8Helpers::SetMethod(isolate, tpl, "rotateVec3", QuaternionRotateVector3);
                V8Helpers::SetMethod(isolate, tpl, "slerp", QuaternionSlerp);
            });

        rootModule->AddClass(vec3Class);
    }
} // namespace Framework::Scripting::Builtins
