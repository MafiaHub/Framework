#pragma once

#include <v8pp/class.hpp>
#include <v8pp/property.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace Framework::Scripting::Builtins {

/**
 * Vector2 wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::vec2.
 */
class Vector2 {
  public:
    Vector2() : _vec(0.0f) {}
    Vector2(float x, float y) : _vec(x, y) {}
    Vector2(const glm::vec2& v) : _vec(v) {}

    // Property accessors
    float getX() const { return _vec.x; }
    void setX(float v) { _vec.x = v; }
    float getY() const { return _vec.y; }
    void setY(float v) { _vec.y = v; }
    float getLength() const { return glm::length(_vec); }
    float getLengthSquared() const { return glm::dot(_vec, _vec); }

    // Methods
    Vector2 add(const Vector2& other) const { return Vector2(_vec + other._vec); }
    Vector2 sub(const Vector2& other) const { return Vector2(_vec - other._vec); }
    Vector2 mul(float scalar) const { return Vector2(_vec * scalar); }
    Vector2 div(float scalar) const { return Vector2(_vec / scalar); }
    float dot(const Vector2& other) const { return glm::dot(_vec, other._vec); }
    Vector2 normalize() const;
    Vector2 lerp(const Vector2& target, float t) const { return Vector2(glm::mix(_vec, target._vec, t)); }
    float distance(const Vector2& other) const { return glm::distance(_vec, other._vec); }
    Vector2 clone() const { return Vector2(_vec); }
    std::string toString() const;

    // Static factory methods
    static Vector2 zero() { return Vector2(0, 0); }
    static Vector2 one() { return Vector2(1, 1); }

    // Access underlying GLM type
    const glm::vec2& vec() const { return _vec; }

    // V8 Registration
    static void Register(v8::Isolate* isolate, v8::Local<v8::Object> global);
    static v8pp::class_<Vector2>& GetClass(v8::Isolate* isolate);

  private:
    glm::vec2 _vec;
    static std::unique_ptr<v8pp::class_<Vector2>> _class;
};

} // namespace Framework::Scripting::Builtins
