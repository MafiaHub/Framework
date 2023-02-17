/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../../../keys.h"
#include "../resource.h"

#include <glm/glm.hpp>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Scripting::Engines::Node::Builtins {
    class Vector3 {
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
            glm::vec3 newVec(x, y, z);
            _data += newVec;
        }

        void Sub(double x, double y, double z) {
            glm::vec3 newVec(x, y, z);
            _data -= newVec;
        }

        void Mul(double x, double y, double z) {
            glm::vec3 newVec(x, y, z);
            _data *= newVec;
        }

        void Div(double x, double y, double z) {
            glm::vec3 newVec(x, y, z);
            _data /= newVec;
        }

        void Lerp(double x, double y, double z, double f) {
            glm::vec3 newVec(x, y, z);
            _data = glm::mix(_data, newVec, static_cast<float>(f));
        };

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<Vector3> cls(isolate);
            cls.ctor<double, double, double>();
            cls.property("x", &Vector3::GetX);
            cls.property("y", &Vector3::GetY);
            cls.property("z", &Vector3::GetZ);
            cls.property("length", &Vector3::GetLength);
            cls.function("toString", &Vector3::ToString);
            cls.function("toArray", &Vector3::ToArray);
            cls.function("add", &Vector3::Add);
            cls.function("sub", &Vector3::Sub);
            cls.function("mul", &Vector3::Mul);
            cls.function("div", &Vector3::Div);
            cls.function("lerp", &Vector3::Lerp);

            rootModule->class_("Vector3", cls);
        }
    };
} // namespace Framework::Scripting::Engines::Node::Builtins
