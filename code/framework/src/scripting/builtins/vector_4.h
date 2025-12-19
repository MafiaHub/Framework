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
#include <sstream>
#include <vector>

namespace Framework::Scripting::Builtins {
    class Vector4 final {
      private:
        glm::vec4 _data;

      public:
        Vector4(double x, double y, double z, double w) {
            _data = {x, y, z, w};
        }

        Vector4(glm::vec4 data) {
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

        double GetW() const {
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
            ss << std::fixed << std::setprecision(4) << "Vector4{ x: " << _data.x << ", y: " << _data.y << ", z: " << _data.z << ", w: " << _data.w << "}";
            return ss.str();
        }

        sol::as_table_t<std::array<double, 4>> ToArray() const {
            return sol::as_table(std::array<double, 4> {_data.x, _data.y, _data.z, _data.w});
        }

        void Add(double x, double y, double z, double w) {
            const glm::vec4 newVec(x, y, z, w);
            _data += newVec;
        }

        void Sub(double x, double y, double z, double w) {
            const glm::vec4 newVec(x, y, z, w);
            _data -= newVec;
        }

        void Mul(double x, double y, double z, double w) {
            const glm::vec4 newVec(x, y, z, w);
            _data *= newVec;
        }

        void Div(double x, double y, double z, double w) {
            const glm::vec4 newVec(x, y, z, w);
            _data /= newVec;
        }

        Vector4 operator+(const Vector4 &other) const {
            return Vector4(_data + other._data);
        }

        Vector4 operator-(const Vector4 &other) const {
            return Vector4(_data - other._data);
        }

        Vector4 operator*(const Vector4 &other) const {
            return Vector4(_data * other._data);
        }

        Vector4 operator/(const Vector4 &other) const {
            return Vector4(_data / other._data);
        }

        void Lerp(double x, double y, double z, double w, double f) {
            const glm::vec4 newVec(x, y, z, w);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        };

        static Vector4 Zero() {
            return Vector4(0.0, 0.0, 0.0, 0.0);
        }

        operator glm::vec4() const {
            return _data;
        }

        static void Register(sol::state *luaEngine) {
            sol::usertype<Vector4> cls              = luaEngine->new_usertype<Vector4>("Vector4", sol::constructors<Vector4(double, double, double, double)>());
            cls["x"]                                = sol::property([](const Vector4 &self) {
                return self.GetX();
            });
            cls["y"]                                = sol::property([](const Vector4 &self) {
                return self.GetY();
            });
            cls["z"]                                = sol::property([](const Vector4 &self) {
                return self.GetZ();
            });
            cls["w"]                                = sol::property([](const Vector4 &self) {
                return self.GetW();
            });
            cls["length"]                           = sol::property([](const Vector4 &self) {
                return self.GetLength();
            });
            cls["lengthSquared"]                    = sol::property([](const Vector4 &self) {
                return self.GetLengthSquared();
            });
            cls["__tostring"]                       = &Vector4::ToString;
            cls["toArray"]                          = &Vector4::ToArray;
            cls["add"]                              = &Vector4::Add;
            cls["sub"]                              = &Vector4::Sub;
            cls["mul"]                              = &Vector4::Mul;
            cls["div"]                              = &Vector4::Div;
            cls[sol::meta_function::addition]       = &Vector4::operator+;
            cls[sol::meta_function::subtraction]    = &Vector4::operator-;
            cls[sol::meta_function::multiplication] = &Vector4::operator*;
            cls[sol::meta_function::division]       = &Vector4::operator/;
            cls["lerp"]                             = &Vector4::Lerp;
            cls["zero"]                             = sol::property(&Vector4::Zero);
        }
    };
} // namespace Framework::Scripting::Builtins
