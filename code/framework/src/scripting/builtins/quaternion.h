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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {

// Forward declaration
class Vector3;

/**
 * Quaternion wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::quat.
 * Uses (w, x, y, z) order internally matching GLM.
 */
class Quaternion final {
  public:
    Quaternion() : _quat(1.0f, 0.0f, 0.0f, 0.0f) {}
    Quaternion(float w, float x, float y, float z) : _quat(w, x, y, z) {}
    Quaternion(const glm::quat& q) : _quat(q) {}

    // Property accessors
    float getW() const { return _quat.w; }
    void setW(float v) { _quat.w = v; }
    float getX() const { return _quat.x; }
    void setX(float v) { _quat.x = v; }
    float getY() const { return _quat.y; }
    void setY(float v) { _quat.y = v; }
    float getZ() const { return _quat.z; }
    void setZ(float v) { _quat.z = v; }

    // Mutable methods (modify in place, return this for chaining)
    Quaternion& multiply(const Quaternion& other) { _quat = _quat * other._quat; return *this; }
    Quaternion& normalize() { _quat = glm::normalize(_quat); return *this; }
    Quaternion& slerp(const Quaternion& target, float t) { _quat = glm::slerp(_quat, target._quat, t); return *this; }
    Quaternion& set(float w, float x, float y, float z) { _quat.w = w; _quat.x = x; _quat.y = y; _quat.z = z; return *this; }

    // Read-only magnitude, mirroring Vector length/lengthSquared. A unit
    // quaternion has length 1; the squared form avoids the square root.
    float getLength() const { return glm::length(_quat); }
    float getLengthSquared() const { return glm::dot(_quat, _quat); }

    // Non-mutating methods
    Quaternion conjugate() const { return Quaternion(glm::conjugate(_quat)); }
    Quaternion inverse() const { return Quaternion(glm::inverse(_quat)); }
    float dot(const Quaternion& other) const { return glm::dot(_quat, other._quat); }
    Vector3 rotateVector(const Vector3& v) const;
    Vector3 toEuler() const;
    Quaternion clone() const { return Quaternion(_quat); }
    std::string toString() const;

    // Static factory methods
    static Quaternion identity() { return Quaternion(1.0f, 0.0f, 0.0f, 0.0f); }
    static Quaternion fromEuler(float pitch, float yaw, float roll);
    static Quaternion fromAxisAngle(const Vector3& axis, float angle);

    // Access underlying GLM type
    const glm::quat& quat() const { return _quat; }

    // V8 Registration
    static void Register(v8::Isolate* isolate, v8::Local<v8::Object> global);
    static v8pp::class_<Quaternion>& GetClass(v8::Isolate* isolate);
    static v8::Local<v8::Object> NewInstance(v8::Isolate* isolate, const glm::quat& value);
    static void UnregisterIsolate(v8::Isolate* isolate);

  private:
    glm::quat _quat;
    static std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Quaternion>>> _classes;
};

} // namespace Framework::Scripting::Builtins
