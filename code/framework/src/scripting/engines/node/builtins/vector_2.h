/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../../../keys.h"
#include "../resource.h"

#include <glm/glm.hpp>
#include <v8pp/module.hpp>
#include <v8pp/class.hpp>

#include <list>
#include <iomanip>
#include <sstream>

namespace Framework::Scripting::Engines::Node::Builtins {
    class Vector2 {
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
            glm::vec2 newVec(x, y);
            _data += newVec;
        }

        void Sub(double x, double y){
            glm::vec2 newVec(x, y);
            _data -= newVec;
        }

        void Mul(double x, double y){
            glm::vec2 newVec(x, y);
            _data *= newVec;
        }

        void Div(double x, double y){
            glm::vec2 newVec(x, y);
            _data *= newVec;
        }

        void Lerp(double x, double y, double f){
            glm::vec2 newVec(x, y);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<Vector2> cls(isolate);
            cls.ctor<double, double>();
            cls.property("x", &Vector2::GetX);
            cls.property("y", &Vector2::GetY);
            cls.property("length", &Vector2::GetLength);
            cls.function("toString", &Vector2::ToString);
            cls.function("toArray", &Vector2::ToArray);
            cls.function("add", &Vector2::Add);
            cls.function("sub", &Vector2::Sub);
            cls.function("mul", &Vector2::Mul);
            cls.function("div", &Vector2::Div);
            cls.function("lerp", &Vector2::Lerp);

            rootModule->class_("Vector2", cls);
        }
    };
} // namespace Framework::Scripting::Builtins
