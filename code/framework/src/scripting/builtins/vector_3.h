/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>
#include <sol/sol.hpp>

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Scripting::Builtins {
    class Vector3 final {
      private:
        glm::vec3 _data;

      public:
        Vector3(double x, double y, double z) {
            _data = {x, y, z};
        }

        double GetX() const {
            return _data.x;
        }

        double GetY() const {
            return _data.y;
        }

        double GetZ() const {
            return _data.z;
        }

        double GetLength() const {
            return glm::length(_data);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4) << "Vector3{ x: " << _data.x << ", y: " << _data.y << ", z: " << _data.z << " }";
            return ss.str();
        }

        std::list<double> ToArray() const {
            return {_data.x, _data.y, _data.z};
        }

        void Add(double x, double y, double z) {
            const glm::vec3 newVec(x, y, z);
            _data += newVec;
        }

        void Sub(double x, double y, double z) {
            const glm::vec3 newVec(x, y, z);
            _data -= newVec;
        }

        void Mul(double x, double y, double z) {
            const glm::vec3 newVec(x, y, z);
            _data *= newVec;
        }

        void Div(double x, double y, double z) {
            const glm::vec3 newVec(x, y, z);
            _data /= newVec;
        }

        void Lerp(double x, double y, double z, double f) {
            const glm::vec3 newVec(x, y, z);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        };

        static void Register(sol::state &luaEngine) {
            sol::usertype<Vector3> cls = luaEngine.new_usertype<Vector3>("Vector3", sol::constructors<Vector3(double, double, double)>());
            cls["x"] = sol::property([](const Vector3& self) { return self.GetX(); });
            cls["y"] = sol::property([](const Vector3& self) { return self.GetY(); });
            cls["z"] = sol::property([](const Vector3& self) { return self.GetZ(); });
            cls["length"] = sol::property([](const Vector3& self) { return self.GetLength(); });
            cls["toString"] = &Vector3::ToString;
            cls["toArray"]  = &Vector3::ToArray;
            cls["add"]     = &Vector3::Add;
            cls["sub"]     = &Vector3::Sub;
            cls["mul"]     = &Vector3::Mul;
            cls["div"]     = &Vector3::Div;
            cls["lerp"]     = &Vector3::Lerp;
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
