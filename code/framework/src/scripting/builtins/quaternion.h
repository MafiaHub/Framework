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
#include <sol/sol.hpp>

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Scripting::Builtins {
    class Quaternion final {
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
            const glm::quat newQuat(w, x, y, z);
            _data += newQuat;
        }

        void Sub(float w, float x, float y, float z) {
            const glm::quat newQuat(w, x, y, z);
            _data -= newQuat;
        }

        void Mul(float w, float x, float y, float z) {
            const glm::quat newQuat(w, x, y, z);
            _data *= newQuat;
        }

        void Lerp(float w, float x, float y, float z, float f) {
            const glm::quat newQuat(w, x, y, z);
            _data = glm::mix(_data, newQuat, f);
        }

        void Conjugate(float w, float x, float y, float z) {
            glm::quat newQuat(w, x, y, z);
            _data = glm::conjugate(_data);
        }

        void Cross(float w, float x, float y, float z) {
            const glm::quat newQuat(w, x, y, z);
            _data = glm::cross(_data, newQuat);
        }

        float Dot(float w, float x, float y, float z) const {
            const glm::quat newQuat(w, x, y, z);
            return glm::dot(_data, newQuat);
        }

        void Inverse() {
            _data = glm::inverse(_data);
        }

        void FromEuler(float x, float y, float z) {
            _data = glm::quat(glm::vec3(x, y, z));
        }

        void FromAxisAngle(float x, float y, float z, float angle) {
            _data = glm::quat(glm::angleAxis(angle, glm::vec3(x, y, z)));
        }

        static void Register(sol::state &luaEngine) {
            sol::usertype<Quaternion> cls = luaEngine.new_usertype<Quaternion>("Quaternion", sol::constructors<Quaternion(float, float, float, float)>());
            cls["w"] = sol::property([](const Quaternion& self) { return self.GetW(); });
            cls["x"] = sol::property([](const Quaternion& self) { return self.GetX(); });
            cls["y"] = sol::property([](const Quaternion& self) { return self.GetY(); });
            cls["z"] = sol::property([](const Quaternion& self) { return self.GetZ(); });
            cls["length"] = sol::property([](const Quaternion& self) { return self.GetLength(); });
            cls["toString"] = &Quaternion::ToString;
            cls["toArray"]  = &Quaternion::ToArray;
            cls["add"]     = &Quaternion::Add;
            cls["sub"]     = &Quaternion::Sub;
            cls["mul"]     = &Quaternion::Mul;
            cls["lerp"]     = &Quaternion::Lerp;
            cls["conjugate"]     = &Quaternion::Conjugate;
            cls["cross"]     = &Quaternion::Cross;
            cls["dot"]     = &Quaternion::Dot;
            cls["inverse"]     = &Quaternion::Inverse;
            cls["fromEuler"]     = &Quaternion::FromEuler;
            cls["fromAxisAngle"]     = &Quaternion::FromAxisAngle;
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
