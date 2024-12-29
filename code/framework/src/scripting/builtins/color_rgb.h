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
    class ColorRGB final {
      private:
        glm::ivec3 _data;

      public:
        ColorRGB(int r, int g, int b) {
            _data = {r, g, b};
        }

        int GetR() const {
            return _data.r;
        }

        int GetG() const {
            return _data.g;
        }

        int GetB() const {
            return _data.b;
        }

        float GetFloatR() const {
            return static_cast<float>(_data.r) / 255.0f;
        }

        float GetFloatG() const {
            return static_cast<float>(_data.g) / 255.0f;
        }

        float GetFloatB() const {
            return static_cast<float>(_data.b) / 255.0f;
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << "RGB{ r: " << _data.r << ", g: " << _data.g << ", b: " << _data.b << " }";
            return ss.str();
        }

        std::list<int> ToArray() const {
            return {_data.r, _data.g, _data.b};
        }

        void Add(int r, int g, int b) {
            const glm::ivec3 newVec(r, g, b);
            _data += newVec;
        }

        void Sub(int r, int g, int b) {
            const glm::ivec3 newVec(r, g, b);
            _data -= newVec;
        }

        void Mul(int r, int g, int b) {
            const glm::ivec3 newVec(r, g, b);
            _data *= newVec;
        }

        void Div(int r, int g, int b) {
            const glm::ivec3 newVec(r, g, b);
            _data /= newVec;
        }

        static ColorRGB FromVec4(const glm::vec4 &vec) {
            return ColorRGB(static_cast<int>(vec.r * 255.0f), static_cast<int>(vec.g * 255.0f), static_cast<int>(vec.b * 255.0f));
        }

        static void Register(sol::state &luaEngine) {
            sol::usertype<ColorRGB> cls = luaEngine.new_usertype<ColorRGB>("RGB", sol::constructors<ColorRGB(int, int, int)>());
            cls["r"] = sol::property([](const ColorRGB& self) { return self.GetR(); });
            cls["g"] = sol::property([](const ColorRGB& self) { return self.GetG(); });
            cls["b"] = sol::property([](const ColorRGB& self) { return self.GetB(); });
            cls["toString"] = &ColorRGB::ToString;
            cls["toArray"]  = &ColorRGB::ToArray;
            cls["add"]     = &ColorRGB::Add;
            cls["sub"]     = &ColorRGB::Sub;
            cls["mul"]     = &ColorRGB::Mul;
            cls["div"]     = &ColorRGB::Div;
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
