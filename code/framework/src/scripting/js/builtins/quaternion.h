#pragma once

#include <v8pp/class.hpp>
#include <v8pp/property.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <memory>
#include <string>

namespace Framework::Scripting::JS::Builtins {

// Forward declaration
class Vector3;

/**
 * Quaternion wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::quat.
 * Uses (w, x, y, z) order internally matching GLM.
 */
class Quaternion {
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

    // Instance methods
    Quaternion multiply(const Quaternion& other) const { return Quaternion(_quat * other._quat); }
    Quaternion normalize() const { return Quaternion(glm::normalize(_quat)); }
    Quaternion conjugate() const { return Quaternion(glm::conjugate(_quat)); }
    Quaternion inverse() const { return Quaternion(glm::inverse(_quat)); }
    Quaternion slerp(const Quaternion& target, float t) const { return Quaternion(glm::slerp(_quat, target._quat, t)); }
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

  private:
    glm::quat _quat;
    static std::unique_ptr<v8pp::class_<Quaternion>> _class;
};

} // namespace Framework::Scripting::JS::Builtins
