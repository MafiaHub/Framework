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

        double GetX() const {
            return _data.x;
        }

        double GetY() const {
            return _data.y;
        }

        double GetLength() const {
            return glm::length(_data);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4) << "Vector2{ x: " << _data.x << ", y: " << _data.y << " }";
            return ss.str();
        }

        std::list<double> ToArray() const {
            return {_data.x, _data.y};
        }

        void Add(double x, double y) {
            const glm::vec2 newVec(x, y);
            _data += newVec;
        }

        void Sub(double x, double y) {
            const glm::vec2 newVec(x, y);
            _data -= newVec;
        }

        void Mul(double x, double y) {
            const glm::vec2 newVec(x, y);
            _data *= newVec;
        }

        void Div(double x, double y) {
            const glm::vec2 newVec(x, y);
            _data /= newVec;
        }

        void Lerp(double x, double y, double f) {
            const glm::vec2 newVec(x, y);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        }

        static void Register(sol::state &luaEngine) {
            sol::usertype<Vector2> cls = luaEngine.new_usertype<Vector2>("Vector2", sol::constructors<Vector2(double, double)>());
            cls["x"] = sol::property([](const Vector2& self) { return self.GetX(); });
            cls["y"] = sol::property([](const Vector2& self) { return self.GetY(); });
            cls["length"] = sol::property([](const Vector2& self) { return self.GetLength(); });
            cls["toString"] = &Vector2::ToString;
            cls["toArray"]  = &Vector2::ToArray;
            cls["add"]     = &Vector2::Add;
            cls["sub"]     = &Vector2::Sub;
            cls["mul"]     = &Vector2::Mul;
            cls["div"]     = &Vector2::Div;
            cls["lerp"]     = &Vector2::Lerp;
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
