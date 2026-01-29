#pragma once

#include <v8pp/class.hpp>
#include <v8pp/property.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <unordered_map>

namespace Framework::Scripting::Builtins {

/**
 * Vector2 wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::vec2.
 */
class Vector2 final {
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

    // Mutable methods (modify in place, return this for chaining)
    Vector2& add(const Vector2& other) { _vec += other._vec; return *this; }
    Vector2& sub(const Vector2& other) { _vec -= other._vec; return *this; }
    Vector2& mul(float scalar) { _vec *= scalar; return *this; }
    Vector2& div(float scalar) { _vec /= scalar; return *this; }
    Vector2& normalize();
    Vector2& lerp(const Vector2& target, float t) { _vec = glm::mix(_vec, target._vec, t); return *this; }
    Vector2& set(float x, float y) { _vec.x = x; _vec.y = y; return *this; }

    // Non-mutating methods
    float dot(const Vector2& other) const { return glm::dot(_vec, other._vec); }
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
    static v8::Local<v8::Object> NewInstance(v8::Isolate* isolate, const glm::vec2& value);

  private:
    glm::vec2 _vec;
    static std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Vector2>>> _classes;
};

} // namespace Framework::Scripting::Builtins
