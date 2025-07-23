/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <sol/sol.hpp>

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Scripting::Builtins {
    class Matrix final {
      private:
        glm::mat4 _data;

      public:
        Matrix() {
            _data = glm::mat4(1.0f);
        }

        Matrix(double value) {
            _data = glm::mat4(static_cast<float>(value));
        }

        double GetElement(int row, int col) const {
            if (row >= 0 && row < 4 && col >= 0 && col < 4) {
                return _data[col][row];
            }
            return 0.0;
        }

        void SetElement(int row, int col, double value) {
            if (row >= 0 && row < 4 && col >= 0 && col < 4) {
                _data[col][row] = static_cast<float>(value);
            }
        }

        double GetDeterminant() const {
            return glm::determinant(_data);
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4) << "Matrix4x4{\n";
            for (int row = 0; row < 4; ++row) {
                ss << "  [";
                for (int col = 0; col < 4; ++col) {
                    ss << _data[col][row];
                    if (col < 3) ss << ", ";
                }
                ss << "]";
                if (row < 3) ss << ",";
                ss << "\n";
            }
            ss << "}";
            return ss.str();
        }

        std::list<double> ToArray() const {
            std::list<double> result;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    result.push_back(_data[col][row]);
                }
            }
            return result;
        }

        void Add(const Matrix& other) {
            _data += other._data;
        }

        void Sub(const Matrix& other) {
            _data -= other._data;
        }

        void Mul(const Matrix& other) {
            _data = _data * other._data;
        }

        void Transpose() {
            _data = glm::transpose(_data);
        }

        void Inverse() {
            _data = glm::inverse(_data);
        }

        void Translate(double x, double y, double z) {
            _data = glm::translate(_data, glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
        }

        void Rotate(double angleRadians, double x, double y, double z) {
            _data = glm::rotate(_data, static_cast<float>(angleRadians), glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
        }

        void Scale(double x, double y, double z) {
            _data = glm::scale(_data, glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
        }

        static Matrix Identity() {
            return Matrix();
        }

        static Matrix Translation(double x, double y, double z) {
            Matrix mat;
            mat.Translate(x, y, z);
            return mat;
        }

        static Matrix Rotation(double angleRadians, double x, double y, double z) {
            Matrix mat;
            mat.Rotate(angleRadians, x, y, z);
            return mat;
        }

        static Matrix Scaling(double x, double y, double z) {
            Matrix mat;
            mat.Scale(x, y, z);
            return mat;
        }

        static void Register(sol::state *luaEngine) {
            sol::usertype<Matrix> cls = luaEngine->new_usertype<Matrix>("Matrix", sol::constructors<Matrix(), Matrix(double)>());
            cls["getElement"] = &Matrix::GetElement;
            cls["setElement"] = &Matrix::SetElement;
            cls["determinant"] = sol::property([](const Matrix& self) { return self.GetDeterminant(); });
            cls["__tostring"] = &Matrix::ToString;
            cls["toArray"] = &Matrix::ToArray;
            cls["add"] = &Matrix::Add;
            cls["sub"] = &Matrix::Sub;
            cls["mul"] = &Matrix::Mul;
            cls["transpose"] = &Matrix::Transpose;
            cls["inverse"] = &Matrix::Inverse;
            cls["translate"] = &Matrix::Translate;
            cls["rotate"] = &Matrix::Rotate;
            cls["scale"] = &Matrix::Scale;
            cls["identity"] = sol::property(&Matrix::Identity);
            cls["translation"] = &Matrix::Translation;
            cls["rotation"] = &Matrix::Rotation;
            cls["scaling"] = &Matrix::Scaling;
        }
    };
} // namespace Framework::Scripting::Builtins
