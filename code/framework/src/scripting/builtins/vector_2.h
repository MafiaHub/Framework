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
    class Vector2 final {
      private:
        glm::vec2 _data;

      public:
        Vector2(double x, double y) {
            _data = {x, y};
        }
        
        Vector2(glm::vec2 data) {
            _data = data;
        }

        double GetX() const {
            return _data.x;
        }

        double GetY() const {
            return _data.y;
        }

        double GetLength() const {
            return glm::length(_data);
        }

        double GetLengthSquared() const {
            return glm::dot(_data, _data);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4) << "Vector2{ x: " << _data.x << ", y: " << _data.y << " }";
            return ss.str();
        }
        
        sol::as_table_t<std::array<double, 2>> ToArray() const {
            return sol::as_table(std::array<double, 2>{_data.x, _data.y});
        }
        
        Vector2 operator+(const Vector2& other) const {
            return Vector2(_data + other._data);
        }

        Vector2 operator-(const Vector2& other) const {
            return Vector2(_data - other._data);
        }

        Vector2 operator*(const Vector2& other) const {
            return Vector2(_data * other._data);
        }

        Vector2 operator/(const Vector2& other) const {
            return Vector2(_data / other._data);
        }

        void Lerp(double x, double y, double f) {
            const glm::vec2 newVec(x, y);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        }

        static Vector2 Zero() {
            return Vector2(0.0, 0.0);
        }

        operator glm::vec2() const {
            return _data;
        }

        static void Register(sol::state *luaEngine) {
            sol::usertype<Vector2> cls = luaEngine->new_usertype<Vector2>("Vector2", sol::constructors<Vector2(double, double)>());
            cls["x"] = sol::property([](const Vector2& self) { return self.GetX(); });
            cls["y"] = sol::property([](const Vector2& self) { return self.GetY(); });
            cls["length"] = sol::property([](const Vector2& self) { return self.GetLength(); });
            cls["lengthSquared"] = sol::property([](const Vector2& self) { return self.GetLengthSquared(); });
            cls["__tostring"] = &Vector2::ToString;
            cls["toArray"]  = &Vector2::ToArray;
            cls[sol::meta_function::addition] = &Vector2::operator+;
            cls[sol::meta_function::subtraction] = &Vector2::operator-;
            cls[sol::meta_function::multiplication] = &Vector2::operator*;
            cls[sol::meta_function::division] = &Vector2::operator/;
            cls["lerp"]     = &Vector2::Lerp;
            cls["zero"] = sol::property(&Vector2::Zero);
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
