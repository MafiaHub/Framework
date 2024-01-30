/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/ext.hpp>
#include <glm/ext/matrix_relational.hpp>
#include <glm/ext/scalar_relational.hpp>
#include <glm/ext/vector_relational.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <iomanip>
#include <list>
#include <sstream>

#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

namespace Framework::Scripting::Engines::Node::Builtins {
    class Quaternion {
      private:
        glm::quat _data;

      public:
        Quaternion(float w, float x, float y, float z) {
            _data = {w, x, y, z};
        }

        float GetW() const {
            return _data.w;
        }

        float GetX() const {
            return _data.x;
        }

        float GetY() const {
            return _data.y;
        }

        float GetZ() const {
            return _data.z;
        }

        float GetLength() const {
            return glm::length(_data);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4) << "Quaternion{ w: " << _data.w << ", x: " << _data.x << ", y: " << _data.y << ", z: " << _data.z << " }";
            return ss.str();
        }

        std::list<float> ToArray() const {
            return {_data.w, _data.x, _data.y, _data.z};
        }

        void Add(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            _data += newQuat;
        }

        void Sub(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            _data -= newQuat;
        }

        void Mul(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            _data *= newQuat;
        }

        void Lerp(float w, float x, float y, float z, float f) {
            glm::quat newQuat(w, x, y, z);
            _data = glm::mix(_data, newQuat, static_cast<float>(f));
        }

        void Conjugate(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            _data = glm::conjugate(_data);
        }

        void Cross(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            _data = glm::cross(_data, newQuat);
        }

        float Dot(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            return glm::dot(_data, newQuat);
        }

        void Inverse() {
            _data = glm::inverse(_data);
        }

        void FromEuler(float x, float y, float z) {
            _data = glm::quat(glm::vec3(x, y, z));
        }

        void FromAxisAngle(float x, float y, float z, float angle) {
            _data = glm::quat(glm::angleAxis(static_cast<float>(angle), glm::vec3(x, y, z)));
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<Quaternion> cls(isolate);
            cls.ctor<float, float, float, float>();
            cls.property("w", &Quaternion::GetW);
            cls.property("x", &Quaternion::GetX);
            cls.property("y", &Quaternion::GetY);
            cls.property("z", &Quaternion::GetZ);
            cls.property("length", &Quaternion::GetLength);
            cls.function("toString", &Quaternion::ToString);
            cls.function("toArray", &Quaternion::ToArray);
            cls.function("add", &Quaternion::Add);
            cls.function("sub", &Quaternion::Sub);
            cls.function("mul", &Quaternion::Mul);
            cls.function("lerp", &Quaternion::Lerp);
            cls.function("conjugate", &Quaternion::Conjugate);
            cls.function("cross", &Quaternion::Cross);
            cls.function("dot", &Quaternion::Dot);
            cls.function("inverse", &Quaternion::Inverse);
            cls.function("fromEuler", &Quaternion::FromEuler);
            cls.function("fromAxisAngle", &Quaternion::FromAxisAngle);

            rootModule->class_("Quaternion", cls);
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
