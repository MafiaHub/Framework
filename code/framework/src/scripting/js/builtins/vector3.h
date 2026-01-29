#pragma once

#include <v8pp/class.hpp>
#include <v8pp/property.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace Framework::Scripting::JS::Builtins {

/**
 * Vector3 wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::vec3.
 */
class Vector3 {
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

    // Methods
    Vector3 add(const Vector3& other) const { return Vector3(_vec + other._vec); }
    Vector3 sub(const Vector3& other) const { return Vector3(_vec - other._vec); }
    Vector3 mul(float scalar) const { return Vector3(_vec * scalar); }
    Vector3 div(float scalar) const { return Vector3(_vec / scalar); }
    float dot(const Vector3& other) const { return glm::dot(_vec, other._vec); }
    Vector3 cross(const Vector3& other) const { return Vector3(glm::cross(_vec, other._vec)); }
    Vector3 normalize() const;
    Vector3 lerp(const Vector3& target, float t) const { return Vector3(glm::mix(_vec, target._vec, t)); }
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

  private:
    glm::vec3 _vec;
    static std::unique_ptr<v8pp::class_<Vector3>> _class;
};

} // namespace Framework::Scripting::JS::Builtins
