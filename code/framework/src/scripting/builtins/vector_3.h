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
#include <vector>
#include <sstream>

namespace Framework::Scripting::Builtins {
    class Vector3 final {
      private:
        glm::vec3 _data;

      public:
        Vector3(double x, double y, double z) {
            _data = {x, y, z};
        }
        
        Vector3(glm::vec3 data) {
            _data = data;
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

        double GetLengthSquared() const {
            return glm::dot(_data, _data);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4) << "Vector3{ x: " << _data.x << ", y: " << _data.y << ", z: " << _data.z << " }";
            return ss.str();
        }

        sol::as_table_t<std::array<double, 3>> ToArray() const {
            return sol::as_table(std::array<double, 3>{_data.x, _data.y, _data.z});
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
        
        Vector3 operator+(const Vector3& other) const {
            return Vector3(_data + other._data);
        }

        Vector3 operator-(const Vector3& other) const {
            return Vector3(_data - other._data);
        }

        Vector3 operator*(const Vector3& other) const {
            return Vector3(_data * other._data);
        }

        Vector3 operator/(const Vector3& other) const {
            return Vector3(_data / other._data);
        }

        void Lerp(double x, double y, double z, double f) {
            const glm::vec3 newVec(x, y, z);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        };

        static Vector3 Zero() {
            return Vector3(0.0, 0.0, 0.0);
        }
        
        operator glm::vec3() const{
            return _data;
        }

        static void Register(sol::state *luaEngine) {
            sol::usertype<Vector3> cls = luaEngine->new_usertype<Vector3>("Vector3", sol::constructors<Vector3(double, double, double)>());
            cls["x"] = sol::property([](const Vector3& self) { return self.GetX(); });
            cls["y"] = sol::property([](const Vector3& self) { return self.GetY(); });
            cls["z"] = sol::property([](const Vector3& self) { return self.GetZ(); });
            cls["length"] = sol::property([](const Vector3& self) { return self.GetLength(); });
            cls["lengthSquared"] = sol::property([](const Vector3& self) { return self.GetLengthSquared(); });
            cls["__tostring"] = &Vector3::ToString;
            cls["toArray"]  = &Vector3::ToArray;
            cls["add"]     = &Vector3::Add;
            cls["sub"]     = &Vector3::Sub;
            cls["mul"]     = &Vector3::Mul;
            cls["div"]     = &Vector3::Div;
            cls[sol::meta_function::addition] = &Vector3::operator+;
            cls[sol::meta_function::subtraction] = &Vector3::operator-;
            cls[sol::meta_function::multiplication] = &Vector3::operator*;
            cls[sol::meta_function::division] = &Vector3::operator/;
            cls["lerp"]     = &Vector3::Lerp;
            cls["zero"] = sol::property(&Vector3::Zero);
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
