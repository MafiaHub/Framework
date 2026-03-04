#pragma once

#include <v8pp/class.hpp>
#include <v8pp/property.hpp>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>

namespace Framework::Scripting::Builtins {

/**
 * Color wrapper class for V8 bindings using v8pp.
 * Provides a clean C++ interface that wraps glm::vec4 for RGBA colors.
 * Components are stored as floats in range [0, 1].
 */
class Color final {
  public:
    Color() : _color(1.0f) {}
    Color(float r, float g, float b, float a = 1.0f) : _color(r, g, b, a) {}
    Color(const glm::vec4& c) : _color(c) {}

    // Property accessors
    float getR() const { return _color.r; }
    void setR(float v) { _color.r = v; }
    float getG() const { return _color.g; }
    void setG(float v) { _color.g = v; }
    float getB() const { return _color.b; }
    void setB(float v) { _color.b = v; }
    float getA() const { return _color.a; }
    void setA(float v) { _color.a = v; }

    // Mutable methods (modify in place, return this for chaining)
    Color& lerp(const Color& target, float t) { _color = glm::mix(_color, target._color, t); return *this; }
    Color& set(float r, float g, float b, float a = 1.0f) { _color.r = r; _color.g = g; _color.b = b; _color.a = a; return *this; }

    // Non-mutating methods
    Color clone() const { return Color(_color); }
    std::string toHex(bool includeAlpha = false) const;
    std::string toString() const;

    // Static factory methods
    static Color fromHex(std::string_view hex);
    static Color fromRGB(float r, float g, float b, float a = 255.0f);
    static Color white() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
    static Color black() { return Color(0.0f, 0.0f, 0.0f, 1.0f); }
    static Color red() { return Color(1.0f, 0.0f, 0.0f, 1.0f); }
    static Color green() { return Color(0.0f, 1.0f, 0.0f, 1.0f); }
    static Color blue() { return Color(0.0f, 0.0f, 1.0f, 1.0f); }
    static Color yellow() { return Color(1.0f, 1.0f, 0.0f, 1.0f); }
    static Color cyan() { return Color(0.0f, 1.0f, 1.0f, 1.0f); }
    static Color magenta() { return Color(1.0f, 0.0f, 1.0f, 1.0f); }
    static Color transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }

    // Access underlying GLM type
    const glm::vec4& vec() const { return _color; }

    // V8 Registration
    static void Register(v8::Isolate* isolate, v8::Local<v8::Object> global);
    static v8pp::class_<Color>& GetClass(v8::Isolate* isolate);

  private:
    glm::vec4 _color;
    static std::unordered_map<v8::Isolate*, std::unique_ptr<v8pp::class_<Color>>> _classes;
};

} // namespace Framework::Scripting::Builtins
