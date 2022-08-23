/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../../../keys.h"
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
        Helpers::SafeToNumber(Helpers::Get(ctx, obj, "w"), ctx, w);
        Helpers::SafeToNumber(Helpers::Get(ctx, obj, "x"), ctx, x);
        Helpers::SafeToNumber(Helpers::Get(ctx, obj, "y"), ctx, y);
        Helpers::SafeToNumber(Helpers::Get(ctx, obj, "z"), ctx, z);
    }

    static void QuaternionConstructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_VALIDATE_CTOR_CALL();

        V8_GET_SELF();
        V8_DEFINE_STACK();

        double w, x, y, z;
        if (!Helpers::GetQuat(ctx, stack, w, x, y, z)) {
            Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        V8_DEF_PROP("w", v8::Number::New(isolate, w));
        V8_DEF_PROP("x", v8::Number::New(isolate, x));
        V8_DEF_PROP("y", v8::Number::New(isolate, y));
        V8_DEF_PROP("z", v8::Number::New(isolate, z));
    }

    static void QuaternionConj(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        V8_RETURN(CreateQuaternion(V8_RES_GETROOT(resource), ctx, glm::conjugate(oldQuat)));
    }

    static void QuaternionCross(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        // Acquire old values
        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::quat newQuat(newW, newX, newY, newZ);
        V8_RETURN(CreateQuaternion(V8_RES_GETROOT(resource), ctx, glm::cross(oldQuat, newQuat)));
    }

    static void QuaternionDot(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
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
        V8_RETURN(CreateQuaternion(V8_RES_GETROOT(resource), ctx, glm::inverse(oldQuat)));
    }

    static void QuaternionRotateVector3(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        V8_DEFINE_STACK();

        double pX, pY, pZ;
        if (!Helpers::GetVec3(ctx, stack, pX, pY, pZ)) {
            Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::vec3 point(pX, pY, pZ);
        V8_RETURN(CreateVector3(V8_RES_GETROOT(resource), ctx, oldQuat * point));
    }

    static void QuaternionSlerp(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newW, newX, newY, newZ;
        if (!Helpers::GetQuat(ctx, stack, newW, newX, newY, newZ)) {
            Helpers::Throw(isolate, "Argument must be a Quaternion or an array of 4 numbers");
            return;
        }

        // Acquire factor
        double f;
        Helpers::SafeToNumber(info[4], ctx, f);

        // Acquire old values
        double w, x, y, z;
        QuatExtract(ctx, _this, w, x, y, z);

        // Construct our objects
        glm::quat oldQuat(w, x, y, z);
        glm::quat newQuat(newW, newX, newY, newZ);
        V8_RETURN(CreateQuaternion(V8_RES_GETROOT(resource), ctx, glm::mix(oldQuat, newQuat, static_cast<float>(f))));
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
        (void)arr->Set(ctx, 0, v8::Number::New(isolate, w));
        (void)arr->Set(ctx, 1, v8::Number::New(isolate, x));
        (void)arr->Set(ctx, 2, v8::Number::New(isolate, y));
        (void)arr->Set(ctx, 3, v8::Number::New(isolate, z));
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
        if (!Helpers::GetVec3(ctx, stack, x, y, z)) {
            Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }
        glm::quat newQuat(glm::vec3(x, y, z));

        V8_RETURN(CreateQuaternion(V8_RES_GETROOT(resource), ctx, newQuat));
    }

    static void QuaternionFromAxisAngle(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        double angle, axisX, axisY, axisZ;
        Helpers::SafeToNumber(info[0], ctx, angle);
        Helpers::SafeToNumber(info[1], ctx, axisX);
        Helpers::SafeToNumber(info[2], ctx, axisY);
        Helpers::SafeToNumber(info[3], ctx, axisZ);

        glm::quat newQuat(glm::angleAxis(static_cast<float>(angle), glm::vec3(axisX, axisY, axisZ)));

        V8_RETURN(CreateQuaternion(V8_RES_GETROOT(resource), ctx, newQuat));
    }

    static void QuaternionRegister(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto quatClass = new Helpers::V8Class(
            GetKeyName(Keys::KEY_QUATERNION), QuaternionConstructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();
                Helpers::SetAccessor(isolate, tpl, "length", QuaternionLength);

                Helpers::SetStaticMethod(isolate, tpl, "fromEuler", QuaternionFromEuler);
                Helpers::SetStaticMethod(isolate, tpl, "fromAxisAngle", QuaternionFromAxisAngle);

                Helpers::SetMethod(isolate, tpl, "conj", QuaternionConj);
                Helpers::SetMethod(isolate, tpl, "dot", QuaternionDot);
                Helpers::SetMethod(isolate, tpl, "cross", QuaternionCross);
                Helpers::SetMethod(isolate, tpl, "inverse", QuaternionInverse);
                Helpers::SetMethod(isolate, tpl, "toArray", QuaternionToArray);
                Helpers::SetMethod(isolate, tpl, "toString", QuaternionToString);
                Helpers::SetMethod(isolate, tpl, "rotateVec3", QuaternionRotateVector3);
                Helpers::SetMethod(isolate, tpl, "slerp", QuaternionSlerp);
            });

        rootModule->AddClass(quatClass);
    }
} // namespace Framework::Scripting::Builtins
