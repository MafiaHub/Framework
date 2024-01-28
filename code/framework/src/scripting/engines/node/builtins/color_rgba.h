/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Scripting::Engines::Node::Builtins {
    class ColorRGBA {
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

        std::string ToString() const {
            std::ostringstream ss;
            ss << "RGBA{ r: " << _data.r << ", g: " << _data.g << ", b: " << _data.b << ", a: " << _data.a << " }";
            return ss.str();
        }

        std::list<int> ToArray() const {
            return {_data.r, _data.g, _data.b, _data.a};
        }

        void Add(int r, int g, int b, int a) {
            glm::ivec4 newVec(r, g, b, a);
            _data += newVec;
        }

        void Sub(int r, int g, int b, int a) {
            glm::ivec4 newVec(r, g, b, a);
            _data -= newVec;
        }

        void Mul(int r, int g, int b, int a) {
            glm::ivec4 newVec(r, g, b, a);
            _data *= newVec;
        }

        void Div(int r, int g, int b, int a) {
            glm::ivec4 newVec(r, g, b, a);
            _data /= newVec;
        }

        static ColorRGBA FromVec4(const glm::vec4 &vec) {
            return ColorRGBA(static_cast<int>(vec.r * 255.0f), static_cast<int>(vec.g * 255.0f), static_cast<int>(vec.b * 255.0f), static_cast<int>(vec.a * 255.0f));
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<ColorRGBA> cls(isolate);
            cls.ctor<int, int, int, int>();
            cls.property("r", &ColorRGBA::GetR);
            cls.property("g", &ColorRGBA::GetG);
            cls.property("b", &ColorRGBA::GetB);
            cls.property("a", &ColorRGBA::GetA);
            cls.function("toString", &ColorRGBA::ToString);
            cls.function("toArray", &ColorRGBA::ToArray);
            cls.function("add", &ColorRGBA::Add);
            cls.function("sub", &ColorRGBA::Sub);
            cls.function("mul", &ColorRGBA::Mul);
            cls.function("div", &ColorRGBA::Div);

            rootModule->class_("RGBA", cls);
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
