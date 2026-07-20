/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8pp/class.hpp>
#include <v8pp/property.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <unordered_map>

namespace Framework::Scripting::Builtins {

/**
 * Vector4 wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::vec4.
 */
class Vector4 final {
  public:
    Vector4() : _vec(0.0f) {}
    Vector4(float x, float y, float z, float w) : _vec(x, y, z, w) {}
    Vector4(const glm::vec4& v) : _vec(v) {}

    // Property accessors
    float getX() const { return _vec.x; }
    void setX(float v) { _vec.x = v; }
    float getY() const { return _vec.y; }
    void setY(float v) { _vec.y = v; }
    float getZ() const { return _vec.z; }
    void setZ(float v) { _vec.z = v; }
    float getW() const { return _vec.w; }
    void setW(float v) { _vec.w = v; }
    float getLength() const { return glm::length(_vec); }
    float getLengthSquared() const { return glm::dot(_vec, _vec); }

    // Mutable methods (modify in place, return this for chaining)
    Vector4& add(const Vector4& other) { _vec += other._vec; return *this; }
    Vector4& sub(const Vector4& other) { _vec -= other._vec; return *this; }
    Vector4& mul(float scalar) { _vec *= scalar; return *this; }
    Vector4& div(float scalar) { _vec /= scalar; return *this; }
    Vector4& normalize();
    Vector4& lerp(const Vector4& target, float t) { _vec = glm::mix(_vec, target._vec, t); return *this; }
    Vector4& set(float x, float y, float z, float w) { _vec.x = x; _vec.y = y; _vec.z = z; _vec.w = w; return *this; }

    // Non-mutating methods
    float dot(const Vector4& other) const { return glm::dot(_vec, other._vec); }
    float distance(const Vector4& other) const { return glm::distance(_vec, other._vec); }
    Vector4 clone() const { return Vector4(_vec); }
    std::string toString() const;

    // Static factory methods
    static Vector4 zero() { return Vector4(0, 0, 0, 0); }
    static Vector4 one() { return Vector4(1, 1, 1, 1); }

    // Access underlying GLM type
    const glm::vec4& vec() const { return _vec; }

    // V8 Registration
    static void Register(v8::Isolate* isolate, v8::Local<v8::Object> global);
    static v8pp::class_<Vector4>& GetClass(v8::Isolate* isolate);
    static v8::Local<v8::Object> NewInstance(v8::Isolate* isolate, const glm::vec4& value);
    static void UnregisterIsolate(v8::Isolate* isolate);

  private:
    glm::vec4 _vec;
    static std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Vector4>>> _classes;
};

} // namespace Framework::Scripting::Builtins
