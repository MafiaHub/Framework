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
 * Vector3 wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::vec3.
 */
class Vector3 final {
  public:
    Vector3() : _vec(0.0f) {}
    Vector3(float x, float y, float z) : _vec(x, y, z) {}
    Vector3(const glm::vec3& v) : _vec(v) {}

    // Property accessors
    float getX() const { return _vec.x; }
    void setX(float v) { _vec.x = v; }
    float getY() const { return _vec.y; }
    void setY(float v) { _vec.y = v; }
    float getZ() const { return _vec.z; }
    void setZ(float v) { _vec.z = v; }
    float getLength() const { return glm::length(_vec); }
    float getLengthSquared() const { return glm::dot(_vec, _vec); }

    // Mutable methods (modify in place, return this for chaining)
    Vector3& add(const Vector3& other) { _vec += other._vec; return *this; }
    Vector3& sub(const Vector3& other) { _vec -= other._vec; return *this; }
    Vector3& mul(float scalar) { _vec *= scalar; return *this; }
    Vector3& div(float scalar) { _vec /= scalar; return *this; }
    Vector3& cross(const Vector3& other) { _vec = glm::cross(_vec, other._vec); return *this; }
    Vector3& normalize();
    Vector3& lerp(const Vector3& target, float t) { _vec = glm::mix(_vec, target._vec, t); return *this; }
    Vector3& set(float x, float y, float z) { _vec.x = x; _vec.y = y; _vec.z = z; return *this; }

    // Non-mutating methods
    float dot(const Vector3& other) const { return glm::dot(_vec, other._vec); }
    float distance(const Vector3& other) const { return glm::distance(_vec, other._vec); }
    Vector3 clone() const { return Vector3(_vec); }
    std::string toString() const;

    // Static factory methods
    static Vector3 zero() { return Vector3(0, 0, 0); }
    static Vector3 one() { return Vector3(1, 1, 1); }
    static Vector3 up() { return Vector3(0, 1, 0); }
    static Vector3 forward() { return Vector3(0, 0, 1); }
    static Vector3 right() { return Vector3(1, 0, 0); }

    // Access underlying GLM type
    const glm::vec3& vec() const { return _vec; }

    // V8 Registration
    static void Register(v8::Isolate* isolate, v8::Local<v8::Object> global);
    static v8pp::class_<Vector3>& GetClass(v8::Isolate* isolate);
    static v8::Local<v8::Object> NewInstance(v8::Isolate* isolate, const glm::vec3& value);
    static void UnregisterIsolate(v8::Isolate* isolate);

  private:
    glm::vec3 _vec;
    static std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Vector3>>> _classes;
};

} // namespace Framework::Scripting::Builtins
