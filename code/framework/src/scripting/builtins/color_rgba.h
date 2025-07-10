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
    class ColorRGBA final {
      private:
        glm::ivec4 _data;

      public:
        ColorRGBA(int r, int g, int b, int a) {
            _data = {r, g, b, a};
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

        int GetA() const {
            return _data.a;
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

        float GetFloatA() const {
            return static_cast<float>(_data.a) / 255.0f;
        }

        ColorRGBA Random() const {
            // TODO: Replace with a better rand library
            return ColorRGBA(rand() % 255, rand() % 255, rand() % 255, rand() % 255);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << "RGBA{ r: " << _data.r << ", g: " << _data.g << ", b: " << _data.b << ", a: " << _data.a << " }";
            return ss.str();
        }

        std::list<int> ToArray() const {
            return {_data.r, _data.g, _data.b, _data.a};
        }

        int ToInteger() const {
            return (_data.r << 24) | (_data.g << 16) | (_data.b << 8) | _data.a;
        }

        void FromInteger(int color) {
            _data.r = (color >> 24) & 0xFF;
            _data.g = (color >> 16) & 0xFF;
            _data.b = (color >> 8) & 0xFF;
            _data.a = color & 0xFF;
        }

        void Add(int r, int g, int b, int a) {
            const glm::ivec4 newVec(r, g, b, a);
            _data += newVec;
        }

        void Sub(int r, int g, int b, int a) {
            const glm::ivec4 newVec(r, g, b, a);
            _data -= newVec;
        }

        void Mul(int r, int g, int b, int a) {
            const glm::ivec4 newVec(r, g, b, a);
            _data *= newVec;
        }

        void Div(int r, int g, int b, int a) {
            const glm::ivec4 newVec(r, g, b, a);
            _data /= newVec;
        }

        static ColorRGBA FromVec4(const glm::vec4 &vec) {
            return ColorRGBA(static_cast<int>(vec.r * 255.0f), static_cast<int>(vec.g * 255.0f), static_cast<int>(vec.b * 255.0f), static_cast<int>(vec.a * 255.0f));
        }

        static void Register(sol::state *luaEngine) {
            sol::usertype<ColorRGBA> cls = luaEngine->new_usertype<ColorRGBA>("RGBA", sol::constructors<ColorRGBA(int, int, int, int)>());
            cls["r"] = sol::property([](const ColorRGBA& self) { return self.GetR(); });
            cls["g"] = sol::property([](const ColorRGBA& self) { return self.GetG(); });
            cls["b"] = sol::property([](const ColorRGBA& self) { return self.GetB(); });
            cls["a"] = sol::property([](const ColorRGBA& self) { return self.GetA(); });
            cls["__tostring"] = &ColorRGBA::ToString;
            cls["toArray"]  = &ColorRGBA::ToArray;
            cls["toInteger"]   = &ColorRGBA::ToInteger;
            cls["fromInteger"] = &ColorRGBA::FromInteger;
            cls["random"]      = &ColorRGBA::Random;
            cls["add"]     = &ColorRGBA::Add;
            cls["sub"]     = &ColorRGBA::Sub;
            cls["mul"]     = &ColorRGBA::Mul;
            cls["div"]     = &ColorRGBA::Div;
            cls["getFloatR"]             = &ColorRGBA::GetFloatR;
            cls["getFloatG"]             = &ColorRGBA::GetFloatG;
            cls["getFloatB"]             = &ColorRGBA::GetFloatB;
            cls["getFloatA"]             = &ColorRGBA::GetFloatA;
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
