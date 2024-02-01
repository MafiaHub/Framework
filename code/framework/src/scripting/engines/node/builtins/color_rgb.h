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
    class ColorRGB {
      protected:
        glm::ivec4 _data;

      public:
        ColorRGB(int r, int g, int b) {
            _data = {r, g, b, 255};
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
            const glm::ivec4 newVec(r, g, b, 0);
            _data += newVec;
        }

        void Sub(int r, int g, int b) {
            const glm::ivec4 newVec(r, g, b, 0);
            _data -= newVec;
        }

        void Mul(int r, int g, int b) {
            const glm::ivec4 newVec(r, g, b, 1);
            _data *= newVec;
        }

        void Div(int r, int g, int b) {
            const glm::ivec4 newVec(r, g, b, 1);
            _data /= newVec;
        }

        static ColorRGB FromVec4(const glm::vec4 &vec) {
            return ColorRGB(static_cast<int>(vec.r * 255.0f), static_cast<int>(vec.g * 255.0f), static_cast<int>(vec.b * 255.0f));
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<ColorRGB> cls(isolate);
            cls.ctor<int, int, int>();
            cls.property("r", &ColorRGB::GetR);
            cls.property("g", &ColorRGB::GetG);
            cls.property("b", &ColorRGB::GetB);
            cls.function("toString", &ColorRGB::ToString);
            cls.function("toArray", &ColorRGB::ToArray);
            cls.function("add", &ColorRGB::Add);
            cls.function("sub", &ColorRGB::Sub);
            cls.function("mul", &ColorRGB::Mul);
            cls.function("div", &ColorRGB::Div);

            rootModule->class_("RGB", cls);
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
