/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "scripting/bind/class.h"
#include "scripting/builtins/builtins.h"
#include "scripting/node_engine.h"
#include "scripting/scripting_catalog.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ValueTypesParityTest {
    namespace Bind = Framework::Scripting::Bind;

    // Takes and returns glm values, which a script sees as the value types in both languages.
    class Anchor final {
      public:
        glm::vec2 flip(const glm::vec2 &value) const {
            return {value.y, value.x};
        }

        glm::vec3 shift(const glm::vec3 &by) const {
            return _position + by;
        }

        glm::vec4 scale(const glm::vec4 &value, float factor) const {
            return value * factor;
        }

        glm::quat turn(const glm::quat &by) const {
            return _rotation * by;
        }

        glm::vec3 position() const {
            return _position;
        }

        void setPosition(const glm::vec3 &position) {
            _position = position;
        }

        glm::quat rotation() const {
            return _rotation;
        }

        void setRotation(const glm::quat &rotation) {
            _rotation = rotation;
        }

        static glm::vec3 origin() {
            return glm::vec3(0.0f);
        }

      private:
        glm::vec3 _position {0.0f};
        glm::quat _rotation {1.0f, 0.0f, 0.0f, 0.0f};
    };
} // namespace ValueTypesParityTest

FW_SCRIPT_CLASS(ValueTypesParityTest::Anchor, "Anchor");

namespace ValueTypesParityTest {
    inline const Bind::Class<Anchor> &AnchorDefinition() {
        using Bind::Doc;
        static const auto definition = Bind::Class<Anchor>("Anchor", "Holds a position and a rotation.")
                                           .Constructor<>(Doc("Creates an anchor at the origin."))
                                           .Method("flip", &Anchor::flip, Doc("Swaps the components.").Param("value", "Vector to flip.").Returns("The flipped vector."))
                                           .Method("shift", &Anchor::shift, Doc("Offsets the position.").Param("by", "Offset to add.").Returns("The offset position."))
                                           .Method("scale", &Anchor::scale, Doc("Scales a vector.").Param("value", "Vector to scale.").Param("factor", "Multiplier.").Returns("The scaled vector."))
                                           .Method("turn", &Anchor::turn, Doc("Composes the rotation.").Param("by", "Rotation applied first.").Returns("The composed rotation."))
                                           .Property("position", &Anchor::position, &Anchor::setPosition, "Where the anchor is.")
                                           .Property("rotation", &Anchor::rotation, &Anchor::setRotation, "How the anchor is turned.")
                                           .Static("origin", &Anchor::origin, Doc("Reads the origin.").Returns("The zero vector."));
        return definition;
    }

    // What an expression gives in both languages: a number, compared within 1e-5, or a text, compared exactly. A
    // boolean is the text "true" or "false".
    struct Expected final {
        Expected(double value): number(value), isNumber(true) {}
        Expected(int value): number(value), isNumber(true) {}
        Expected(const char *value): text(value) {}

        std::string Spelled() const {
            if (!isNumber) {
                return "\"" + text + "\"";
            }
            std::ostringstream spelled;
            spelled << number;
            return spelled.str();
        }

        double number = 0.0;
        bool isNumber = false;
        std::string text;
    };

    // One expression written in each language. Lua calls a method with `:` and constructs with Name.new(...) or
    // Name(...); either source may be a block that ends on the value.
    struct Case final {
        const char *js;
        const char *lua;
        Expected expected;
    };

    inline bool Close(double left, double right) {
        return std::abs(left - right) <= 1e-5;
    }

    // A JavaScript runtime and a Lua state, each with the value types and Anchor as globals.
    class Runtimes final {
      public:
        static constexpr const char *kCatalogName = "value_types_parity";

        Runtimes() {
            using namespace Framework::Scripting;
            _started = _engine.Init() == ScriptingError::SCRIPTING_NONE;
            if (!_started) {
                return;
            }

            Enter([this](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                // A catalog of its own: Anchor is no builtin, and the default catalog is shared by every test.
                SetScriptingCatalog(isolate, kCatalogName);
                v8::Local<v8::Object> global = context->Global();
                Builtins::RegisterValueTypes(isolate, global);
                _anchor = AnchorDefinition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), Bind::Side::Server);
                global->Set(context, v8pp::to_v8(isolate, "Anchor"), _anchor->js_function_template()->GetFunction(context).ToLocalChecked()).Check();
            });

            Builtins::RegisterValueTypesLua(_lua.L, Bind::Side::Server);
            AnchorDefinition().RegisterLuaGlobal(_lua.L, Bind::Side::Server);
        }

        ~Runtimes() {
            if (_started) {
                _anchor.reset();
                Framework::Scripting::ClearScriptingCatalog(_engine.GetIsolate());
            }
            _engine.Shutdown();
        }

        bool Started() const {
            return _started;
        }

        lua_State *Lua() const {
            return _lua.L;
        }

        // Evaluates every case in both languages. Returns one line for each result that is not the expected one,
        // nothing when they all are.
        std::string Mismatches(const std::vector<Case> &cases) {
            std::string mismatches;
            for (const Case &entry : cases) {
                mismatches += JsMismatch(entry.js, entry.expected);
                mismatches += LuaMismatch(entry.lua, entry.expected);
            }
            return mismatches;
        }

        // What `source` gives in JavaScript, as String() spells it, or "<threw> " and the exception.
        std::string Js(const std::string &source) {
            bool isNumber = false;
            double number = 0.0;
            return Js(source, isNumber, number);
        }

        std::string JsMismatch(const std::string &source, const Expected &expected) {
            bool isNumber          = false;
            double number          = 0.0;
            const std::string text = Js(source, isNumber, number);
            const bool matches     = expected.isNumber ? isNumber && Close(number, expected.number) : !isNumber && text == expected.text;
            return matches ? std::string() : Mismatch("js ", source, isNumber ? text : "\"" + text + "\"", expected);
        }

        std::string LuaMismatch(const std::string &source, const Expected &expected) {
            if (expected.isNumber) {
                const double number = LuaTest::EvalNumber(_lua.L, source);
                return Close(number, expected.number) ? std::string() : Mismatch("lua", source, "\"" + LuaTest::Eval(_lua.L, source) + "\"", expected);
            }
            const std::string text = LuaTest::Eval(_lua.L, source);
            return text == expected.text ? std::string() : Mismatch("lua", source, "\"" + text + "\"", expected);
        }

      private:
        template <typename Action>
        void Enter(Action action) {
            v8::Isolate *isolate = _engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _engine.GetContext();
            v8::Context::Scope contextScope(context);
            action(isolate, context);
        }

        std::string Js(const std::string &source, bool &isNumber, double &number) {
            std::string text;
            Enter([&](v8::Isolate *isolate, v8::Local<v8::Context> context) {
                v8::TryCatch tryCatch(isolate);
                v8::Local<v8::Script> script;
                v8::Local<v8::Value> result;
                if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, source.c_str()).ToLocalChecked()).ToLocal(&script) || !script->Run(context).ToLocal(&result)) {
                    v8::String::Utf8Value thrown(isolate, tryCatch.Exception());
                    text = std::string("<threw> ") + (*thrown ? *thrown : "");
                    return;
                }
                if (result->IsNumber()) {
                    isNumber = true;
                    number   = result.As<v8::Number>()->Value();
                }
                v8::String::Utf8Value spelled(isolate, result);
                text = *spelled ? *spelled : "";
            });
            return text;
        }

        static std::string Mismatch(const char *language, const std::string &source, const std::string &got, const Expected &expected) {
            return std::string("\n\t") + language + " `" + source + "` gave " + got + ", expected " + expected.Spelled();
        }

        Framework::Scripting::NodeEngine _engine {{}};
        LuaTest::State _lua;
        std::unique_ptr<v8pp::class_<Anchor>> _anchor;
        bool _started = false;
    };
    inline std::vector<Case> Vector2Cases() {
        return {
            {"new Vector2(1, 2).x", "Vector2.new(1, 2).x", 1},
            {"new Vector2(1, 2).y", "Vector2(1, 2).y", 2},
            {"{ const v = new Vector2(0, 0); v.x = 5; v.y = 6; v.x + v.y * 10 }", "local v = Vector2.new(0, 0); v.x = 5; v.y = 6; return v.x + v.y * 10", 65},
            {"new Vector2(3, 4).length", "Vector2.new(3, 4).length", 5},
            {"new Vector2(3, 4).lengthSquared", "Vector2.new(3, 4).lengthSquared", 25},
            {"new Vector2(1, 2).add(new Vector2(3, 5)).toString()", "Vector2.new(1, 2):add(Vector2.new(3, 5)):toString()", "Vector2(4, 7)"},
            {"new Vector2(1, 2).sub(new Vector2(3, 5)).toString()", "Vector2.new(1, 2):sub(Vector2.new(3, 5)):toString()", "Vector2(-2, -3)"},
            {"new Vector2(1, 2).mul(2.5).toString()", "Vector2.new(1, 2):mul(2.5):toString()", "Vector2(2.5, 5)"},
            {"new Vector2(1, 2).div(4).toString()", "Vector2.new(1, 2):div(4):toString()", "Vector2(0.25, 0.5)"},
            {"new Vector2(1, 2).dot(new Vector2(3, 5))", "Vector2.new(1, 2):dot(Vector2.new(3, 5))", 13},
            {"new Vector2(3, 4).normalize().x", "Vector2.new(3, 4):normalize().x", 0.6},
            {"new Vector2(3, 4).normalize().length", "Vector2.new(3, 4):normalize().length", 1},
            {"new Vector2(0, 0).normalize().toString()", "Vector2.new(0, 0):normalize():toString()", "Vector2(0, 0)"},
            {"new Vector2(0, 0).lerp(new Vector2(10, 20), 0.5).toString()", "Vector2.new(0, 0):lerp(Vector2.new(10, 20), 0.5):toString()", "Vector2(5, 10)"},
            {"new Vector2(0, 0).set(7, 8).toString()", "Vector2.new(0, 0):set(7, 8):toString()", "Vector2(7, 8)"},
            {"new Vector2(1, 1).distance(new Vector2(4, 5))", "Vector2.new(1, 1):distance(Vector2.new(4, 5))", 5},
            {"{ const a = new Vector2(1, 2); const b = a.clone(); b.x = 9; a.x + b.x }", "local a = Vector2.new(1, 2); local b = a:clone(); b.x = 9; return a.x + b.x", 10},
            {"{ const a = new Vector2(1, 2); a.clone() === a }", "local a = Vector2.new(1, 2); return rawequal(a:clone(), a)", "false"},
            {"String(new Vector2(1.5, -2))", "tostring(Vector2.new(1.5, -2))", "Vector2(1.5, -2)"},
            {"{ const a = new Vector2(1, 2); a.add(new Vector2(1, 1)) === a }", "local a = Vector2.new(1, 2); return rawequal(a:add(Vector2.new(1, 1)), a)", "true"},
            {"{ const a = new Vector2(1, 2); a.add(new Vector2(1, 1)).mul(2).sub(new Vector2(1, 0)); a.toString() }", "local a = Vector2.new(1, 2); a:add(Vector2.new(1, 1)):mul(2):sub(Vector2.new(1, 0)); return a:toString()", "Vector2(3, 6)"},
            {"Vector2.zero().toString()", "Vector2.zero():toString()", "Vector2(0, 0)"},
            {"Vector2.one().toString()", "Vector2.one():toString()", "Vector2(1, 1)"},
        };
    }

    inline std::vector<Case> Vector3Cases() {
        return {
            {"new Vector3(1, 2, 3).x", "Vector3.new(1, 2, 3).x", 1},
            {"new Vector3(1, 2, 3).y", "Vector3(1, 2, 3).y", 2},
            {"new Vector3(1, 2, 3).z", "Vector3.new(1, 2, 3).z", 3},
            {"{ const v = new Vector3(0, 0, 0); v.x = 5; v.y = 6; v.z = 7; v.x + v.y * 10 + v.z * 100 }", "local v = Vector3.new(0, 0, 0); v.x = 5; v.y = 6; v.z = 7; return v.x + v.y * 10 + v.z * 100", 765},
            {"new Vector3(3, 4, 0).length", "Vector3.new(3, 4, 0).length", 5},
            {"new Vector3(3, 4, 0).lengthSquared", "Vector3.new(3, 4, 0).lengthSquared", 25},
            {"new Vector3(1, 2, 3).add(new Vector3(4, 5, 6)).toString()", "Vector3.new(1, 2, 3):add(Vector3.new(4, 5, 6)):toString()", "Vector3(5, 7, 9)"},
            {"new Vector3(1, 2, 3).sub(new Vector3(4, 6, 8)).toString()", "Vector3.new(1, 2, 3):sub(Vector3.new(4, 6, 8)):toString()", "Vector3(-3, -4, -5)"},
            {"new Vector3(1, 2, 3).mul(2.5).toString()", "Vector3.new(1, 2, 3):mul(2.5):toString()", "Vector3(2.5, 5, 7.5)"},
            {"new Vector3(1, 2, 3).div(4).toString()", "Vector3.new(1, 2, 3):div(4):toString()", "Vector3(0.25, 0.5, 0.75)"},
            {"new Vector3(1, 2, 3).dot(new Vector3(4, 5, 6))", "Vector3.new(1, 2, 3):dot(Vector3.new(4, 5, 6))", 32},
            {"new Vector3(1, 0, 0).cross(new Vector3(0, 1, 0)).toString()", "Vector3.new(1, 0, 0):cross(Vector3.new(0, 1, 0)):toString()", "Vector3(0, 0, 1)"},
            {"new Vector3(3, 4, 0).normalize().x", "Vector3.new(3, 4, 0):normalize().x", 0.6},
            {"new Vector3(3, 4, 0).normalize().length", "Vector3.new(3, 4, 0):normalize().length", 1},
            {"new Vector3(0, 0, 0).normalize().toString()", "Vector3.new(0, 0, 0):normalize():toString()", "Vector3(0, 0, 0)"},
            {"new Vector3(0, 0, 0).lerp(new Vector3(10, 20, 30), 0.5).toString()", "Vector3.new(0, 0, 0):lerp(Vector3.new(10, 20, 30), 0.5):toString()", "Vector3(5, 10, 15)"},
            {"new Vector3(0, 0, 0).set(7, 8, 9).toString()", "Vector3.new(0, 0, 0):set(7, 8, 9):toString()", "Vector3(7, 8, 9)"},
            {"new Vector3(1, 1, 1).distance(new Vector3(4, 5, 1))", "Vector3.new(1, 1, 1):distance(Vector3.new(4, 5, 1))", 5},
            {"{ const a = new Vector3(1, 2, 3); const b = a.clone(); b.x = 9; a.x + b.x }", "local a = Vector3.new(1, 2, 3); local b = a:clone(); b.x = 9; return a.x + b.x", 10},
            {"{ const a = new Vector3(1, 2, 3); a.clone() === a }", "local a = Vector3.new(1, 2, 3); return rawequal(a:clone(), a)", "false"},
            {"String(new Vector3(1.5, -2, 0.25))", "tostring(Vector3.new(1.5, -2, 0.25))", "Vector3(1.5, -2, 0.25)"},
            {"{ const a = new Vector3(1, 2, 3); a.add(new Vector3(1, 1, 1)) === a }", "local a = Vector3.new(1, 2, 3); return rawequal(a:add(Vector3.new(1, 1, 1)), a)", "true"},
            {"{ const a = new Vector3(1, 2, 3); a.add(new Vector3(1, 1, 1)).mul(2).cross(new Vector3(0, 0, 1)); a.toString() }", "local a = Vector3.new(1, 2, 3); a:add(Vector3.new(1, 1, 1)):mul(2):cross(Vector3.new(0, 0, 1)); return a:toString()", "Vector3(6, -4, 0)"},
            {"Vector3.zero().toString()", "Vector3.zero():toString()", "Vector3(0, 0, 0)"},
            {"Vector3.one().toString()", "Vector3.one():toString()", "Vector3(1, 1, 1)"},
            {"Vector3.up().toString()", "Vector3.up():toString()", "Vector3(0, 1, 0)"},
            {"Vector3.forward().toString()", "Vector3.forward():toString()", "Vector3(0, 0, 1)"},
            {"Vector3.right().toString()", "Vector3.right():toString()", "Vector3(1, 0, 0)"},
        };
    }

    inline std::vector<Case> Vector4Cases() {
        return {
            {"new Vector4(1, 2, 3, 4).x", "Vector4.new(1, 2, 3, 4).x", 1},
            {"new Vector4(1, 2, 3, 4).y", "Vector4(1, 2, 3, 4).y", 2},
            {"new Vector4(1, 2, 3, 4).z", "Vector4.new(1, 2, 3, 4).z", 3},
            {"new Vector4(1, 2, 3, 4).w", "Vector4.new(1, 2, 3, 4).w", 4},
            {"{ const v = new Vector4(0, 0, 0, 0); v.x = 5; v.y = 6; v.z = 7; v.w = 8; v.x + v.y * 10 + v.z * 100 + v.w * 1000 }", "local v = Vector4.new(0, 0, 0, 0); v.x = 5; v.y = 6; v.z = 7; v.w = 8; return v.x + v.y * 10 + v.z * 100 + v.w * 1000", 8765},
            {"new Vector4(1, 2, 2, 4).length", "Vector4.new(1, 2, 2, 4).length", 5},
            {"new Vector4(1, 2, 2, 4).lengthSquared", "Vector4.new(1, 2, 2, 4).lengthSquared", 25},
            {"new Vector4(1, 2, 3, 4).add(new Vector4(4, 5, 6, 7)).toString()", "Vector4.new(1, 2, 3, 4):add(Vector4.new(4, 5, 6, 7)):toString()", "Vector4(5, 7, 9, 11)"},
            {"new Vector4(1, 2, 3, 4).sub(new Vector4(4, 6, 8, 10)).toString()", "Vector4.new(1, 2, 3, 4):sub(Vector4.new(4, 6, 8, 10)):toString()", "Vector4(-3, -4, -5, -6)"},
            {"new Vector4(1, 2, 3, 4).mul(2.5).toString()", "Vector4.new(1, 2, 3, 4):mul(2.5):toString()", "Vector4(2.5, 5, 7.5, 10)"},
            {"new Vector4(1, 2, 3, 4).div(4).toString()", "Vector4.new(1, 2, 3, 4):div(4):toString()", "Vector4(0.25, 0.5, 0.75, 1)"},
            {"new Vector4(1, 2, 3, 4).dot(new Vector4(4, 5, 6, 7))", "Vector4.new(1, 2, 3, 4):dot(Vector4.new(4, 5, 6, 7))", 60},
            {"new Vector4(1, 1, 1, 1).distance(new Vector4(2, 3, 3, 5))", "Vector4.new(1, 1, 1, 1):distance(Vector4.new(2, 3, 3, 5))", 5},
            {"new Vector4(1, 2, 2, 4).normalize().x", "Vector4.new(1, 2, 2, 4):normalize().x", 0.2},
            {"new Vector4(1, 2, 2, 4).normalize().length", "Vector4.new(1, 2, 2, 4):normalize().length", 1},
            {"new Vector4(0, 0, 0, 0).normalize().toString()", "Vector4.new(0, 0, 0, 0):normalize():toString()", "Vector4(0, 0, 0, 0)"},
            {"new Vector4(0, 0, 0, 0).lerp(new Vector4(10, 20, 30, 40), 0.5).toString()", "Vector4.new(0, 0, 0, 0):lerp(Vector4.new(10, 20, 30, 40), 0.5):toString()", "Vector4(5, 10, 15, 20)"},
            {"new Vector4(0, 0, 0, 0).set(6, 7, 8, 9).toString()", "Vector4.new(0, 0, 0, 0):set(6, 7, 8, 9):toString()", "Vector4(6, 7, 8, 9)"},
            {"{ const a = new Vector4(1, 2, 3, 4); const b = a.clone(); b.w = 9; a.w + b.w }", "local a = Vector4.new(1, 2, 3, 4); local b = a:clone(); b.w = 9; return a.w + b.w", 13},
            {"{ const a = new Vector4(1, 2, 3, 4); a.clone() === a }", "local a = Vector4.new(1, 2, 3, 4); return rawequal(a:clone(), a)", "false"},
            {"String(new Vector4(1.5, -2, 0.25, 8))", "tostring(Vector4.new(1.5, -2, 0.25, 8))", "Vector4(1.5, -2, 0.25, 8)"},
            {"{ const a = new Vector4(1, 2, 3, 4); a.add(new Vector4(1, 1, 1, 1)) === a }", "local a = Vector4.new(1, 2, 3, 4); return rawequal(a:add(Vector4.new(1, 1, 1, 1)), a)", "true"},
            {"{ const a = new Vector4(1, 2, 3, 4); a.add(new Vector4(1, 1, 1, 1)).mul(2).div(4); a.toString() }", "local a = Vector4.new(1, 2, 3, 4); a:add(Vector4.new(1, 1, 1, 1)):mul(2):div(4); return a:toString()", "Vector4(1, 1.5, 2, 2.5)"},
            {"Vector4.zero().toString()", "Vector4.zero():toString()", "Vector4(0, 0, 0, 0)"},
            {"Vector4.one().toString()", "Vector4.one():toString()", "Vector4(1, 1, 1, 1)"},
        };
    }

    inline std::vector<Case> QuaternionCases() {
        return {
            {"new Quaternion(1, 2, 3, 4).w", "Quaternion.new(1, 2, 3, 4).w", 1},
            {"new Quaternion(1, 2, 3, 4).x", "Quaternion(1, 2, 3, 4).x", 2},
            {"new Quaternion(1, 2, 3, 4).y", "Quaternion.new(1, 2, 3, 4).y", 3},
            {"new Quaternion(1, 2, 3, 4).z", "Quaternion.new(1, 2, 3, 4).z", 4},
            {"{ const q = new Quaternion(1, 0, 0, 0); q.w = 5; q.x = 6; q.y = 7; q.z = 8; q.w + q.x * 10 + q.y * 100 + q.z * 1000 }", "local q = Quaternion.new(1, 0, 0, 0); q.w = 5; q.x = 6; q.y = 7; q.z = 8; return q.w + q.x * 10 + q.y * 100 + q.z * 1000", 8765},
            {"new Quaternion(0, 3, 4, 0).length", "Quaternion.new(0, 3, 4, 0).length", 5},
            {"new Quaternion(0, 3, 4, 0).lengthSquared", "Quaternion.new(0, 3, 4, 0).lengthSquared", 25},
            {"new Quaternion(0, 1, 0, 0).mul(new Quaternion(0, 0, 1, 0)).z", "Quaternion.new(0, 1, 0, 0):mul(Quaternion.new(0, 0, 1, 0)).z", 1},
            {"new Quaternion(0, 0, 1, 0).mul(new Quaternion(0, 1, 0, 0)).z", "Quaternion.new(0, 0, 1, 0):mul(Quaternion.new(0, 1, 0, 0)).z", -1},
            {"new Quaternion(0, 0, 0, 2).normalize().toString()", "Quaternion.new(0, 0, 0, 2):normalize():toString()", "Quaternion(0, 0, 0, 1)"},
            {"new Quaternion(0, 0, 0, 0).normalize().toString()", "Quaternion.new(0, 0, 0, 0):normalize():toString()", "Quaternion(1, 0, 0, 0)"},
            {"new Quaternion(1, 2, 3, 4).conjugate().toString()", "Quaternion.new(1, 2, 3, 4):conjugate():toString()", "Quaternion(1, -2, -3, -4)"},
            {"new Quaternion(0, 0, 0, 2).inverse().z", "Quaternion.new(0, 0, 0, 2):inverse().z", -0.5},
            {"Quaternion.identity().slerp(new Quaternion(0, 1, 0, 0), 0.5).w", "Quaternion.identity():slerp(Quaternion.new(0, 1, 0, 0), 0.5).w", 0.70710678},
            {"Quaternion.identity().slerp(new Quaternion(0, 1, 0, 0), 0.5).x", "Quaternion.identity():slerp(Quaternion.new(0, 1, 0, 0), 0.5).x", 0.70710678},
            {"new Quaternion(1, 2, 3, 4).dot(new Quaternion(5, 6, 7, 8))", "Quaternion.new(1, 2, 3, 4):dot(Quaternion.new(5, 6, 7, 8))", 70},
            {"Quaternion.fromAxisAngle(new Vector3(0, 0, 1), Math.PI / 2).rotateVector(new Vector3(1, 0, 0)).x", "Quaternion.fromAxisAngle(Vector3.new(0, 0, 1), math.pi / 2):rotateVector(Vector3.new(1, 0, 0)).x", 0},
            {"Quaternion.fromAxisAngle(new Vector3(0, 0, 1), Math.PI / 2).rotateVector(new Vector3(1, 0, 0)).y", "Quaternion.fromAxisAngle(Vector3.new(0, 0, 1), math.pi / 2):rotateVector(Vector3.new(1, 0, 0)).y", 1},
            {"String(Quaternion.identity().rotateVector(new Vector3(1, 2, 3)))", "tostring(Quaternion.identity():rotateVector(Vector3.new(1, 2, 3)))", "Vector3(1, 2, 3)"},
            {"Quaternion.fromEuler(0.1, 0.2, 0.3).toEuler().x", "Quaternion.fromEuler(0.1, 0.2, 0.3):toEuler().x", 0.1},
            {"Quaternion.fromEuler(0.1, 0.2, 0.3).toEuler().y", "Quaternion.fromEuler(0.1, 0.2, 0.3):toEuler().y", 0.2},
            {"Quaternion.fromEuler(0.1, 0.2, 0.3).toEuler().z", "Quaternion.fromEuler(0.1, 0.2, 0.3):toEuler().z", 0.3},
            {"Quaternion.fromEuler(0.1, 0.2, 0.3).toEuler().lengthSquared", "Quaternion.fromEuler(0.1, 0.2, 0.3):toEuler().lengthSquared", 0.14},
            {"new Quaternion(1, 0, 0, 0).set(5, 6, 7, 8).toString()", "Quaternion.new(1, 0, 0, 0):set(5, 6, 7, 8):toString()", "Quaternion(5, 6, 7, 8)"},
            {"{ const a = new Quaternion(1, 2, 3, 4); const b = a.clone(); b.w = 9; a.w + b.w }", "local a = Quaternion.new(1, 2, 3, 4); local b = a:clone(); b.w = 9; return a.w + b.w", 10},
            {"{ const a = new Quaternion(1, 2, 3, 4); a.clone() === a }", "local a = Quaternion.new(1, 2, 3, 4); return rawequal(a:clone(), a)", "false"},
            {"{ const a = new Quaternion(1, 2, 3, 4); a.conjugate(); a.inverse(); a.toString() }", "local a = Quaternion.new(1, 2, 3, 4); a:conjugate(); a:inverse(); return a:toString()", "Quaternion(1, 2, 3, 4)"},
            {"String(new Quaternion(1.5, -2, 0.25, 8))", "tostring(Quaternion.new(1.5, -2, 0.25, 8))", "Quaternion(1.5, -2, 0.25, 8)"},
            {"{ const a = new Quaternion(1, 0, 0, 0); a.mul(new Quaternion(0, 1, 0, 0)) === a }", "local a = Quaternion.new(1, 0, 0, 0); return rawequal(a:mul(Quaternion.new(0, 1, 0, 0)), a)", "true"},
            {"{ const a = new Quaternion(2, 0, 0, 0); a.normalize().slerp(Quaternion.identity(), 1).set(0, 0, 3, 4).normalize(); a.z }", "local a = Quaternion.new(2, 0, 0, 0); a:normalize():slerp(Quaternion.identity(), 1):set(0, 0, 3, 4):normalize(); return a.z", 0.8},
            {"Quaternion.identity().toString()", "Quaternion.identity():toString()", "Quaternion(1, 0, 0, 0)"},
            {"Quaternion.fromEuler(0, 0, 0).toString()", "Quaternion.fromEuler(0, 0, 0):toString()", "Quaternion(1, 0, 0, 0)"},
            {"Quaternion.fromEuler(Math.PI, 0, 0).x", "Quaternion.fromEuler(math.pi, 0, 0).x", 1},
            {"Quaternion.fromAxisAngle(new Vector3(0, 0, 2), Math.PI).z", "Quaternion.fromAxisAngle(Vector3.new(0, 0, 2), math.pi).z", 1},
            {"Quaternion.fromAxisAngle(new Vector3(0, 0, 0), 1).toString()", "Quaternion.fromAxisAngle(Vector3.new(0, 0, 0), 1):toString()", "Quaternion(1, 0, 0, 0)"},
        };
    }

    inline std::vector<Case> ColorCases() {
        return {
            {"new Color(0.25, 0.5, 0.75, 1).r", "Color.new(0.25, 0.5, 0.75, 1).r", 0.25},
            {"new Color(0.25, 0.5, 0.75, 1).g", "Color(0.25, 0.5, 0.75, 1).g", 0.5},
            {"new Color(0.25, 0.5, 0.75, 1).b", "Color.new(0.25, 0.5, 0.75, 1).b", 0.75},
            {"new Color(0.25, 0.5, 0.75, 0.125).a", "Color.new(0.25, 0.5, 0.75, 0.125).a", 0.125},
            {"new Color(0.25, 0.5, 0.75).a", "Color.new(0.25, 0.5, 0.75).a", 1},
            {"new Color(0.25, 0.5, 0.75).toString()", "Color(0.25, 0.5, 0.75):toString()", "Color(0.25, 0.5, 0.75, 1)"},
            {"{ const c = new Color(0, 0, 0, 0); c.r = 0.5; c.g = 0.25; c.b = 0.125; c.a = 1; c.toString() }", "local c = Color.new(0, 0, 0, 0); c.r = 0.5; c.g = 0.25; c.b = 0.125; c.a = 1; return c:toString()", "Color(0.5, 0.25, 0.125, 1)"},
            {"new Color(0, 0, 0, 0).lerp(new Color(1, 1, 1, 1), 0.25).toString()", "Color.new(0, 0, 0, 0):lerp(Color.new(1, 1, 1, 1), 0.25):toString()", "Color(0.25, 0.25, 0.25, 0.25)"},
            {"new Color(0, 0, 0, 0).set(0.5, 0.25, 1).toString()", "Color.new(0, 0, 0, 0):set(0.5, 0.25, 1):toString()", "Color(0.5, 0.25, 1, 1)"},
            {"new Color(0, 0, 0, 0).set(0.5, 0.25, 1, 0.5).toString()", "Color.new(0, 0, 0, 0):set(0.5, 0.25, 1, 0.5):toString()", "Color(0.5, 0.25, 1, 0.5)"},
            {"{ const a = new Color(0.5, 0.5, 0.5); const b = a.clone(); b.r = 1; a.r + b.r }", "local a = Color.new(0.5, 0.5, 0.5); local b = a:clone(); b.r = 1; return a.r + b.r", 1.5},
            {"{ const a = new Color(0.5, 0.5, 0.5); a.clone() === a }", "local a = Color.new(0.5, 0.5, 0.5); return rawequal(a:clone(), a)", "false"},
            {"new Color(1, 0.5, 0).toHex()", "Color.new(1, 0.5, 0):toHex()", "#ff8000"},
            {"new Color(1, 0.5, 0).toHex(true)", "Color.new(1, 0.5, 0):toHex(true)", "#ff8000ff"},
            {"new Color(1, 0.5, 0, 0).toHex(false)", "Color.new(1, 0.5, 0, 0):toHex(false)", "#ff8000"},
            {"String(new Color(1, 0.5, 0, 0.25))", "tostring(Color.new(1, 0.5, 0, 0.25))", "Color(1, 0.5, 0, 0.25)"},
            {"{ const a = new Color(0, 0, 0); a.lerp(new Color(1, 1, 1), 0.5) === a }", "local a = Color.new(0, 0, 0); return rawequal(a:lerp(Color.new(1, 1, 1), 0.5), a)", "true"},
            {"{ const a = new Color(0, 0, 0); a.set(1, 1, 1, 0).lerp(new Color(0, 0, 0, 1), 0.5); a.toString() }", "local a = Color.new(0, 0, 0); a:set(1, 1, 1, 0):lerp(Color.new(0, 0, 0, 1), 0.5); return a:toString()", "Color(0.5, 0.5, 0.5, 0.5)"},
            {"Color.fromHex('#ff0000').toString()", "Color.fromHex('#ff0000'):toString()", "Color(1, 0, 0, 1)"},
            {"Color.fromHex('00ff0080').g", "Color.fromHex('00ff0080').g", 1},
            {"Color.fromHex('00ff0080').a", "Color.fromHex('00ff0080').a", 128.0 / 255.0},
            {"Color.fromHex('#f00').toString()", "Color.fromHex('#f00'):toString()", "Color(1, 1, 1, 1)"},
            {"Color.fromRGB(255, 0, 0).toString()", "Color.fromRGB(255, 0, 0):toString()", "Color(1, 0, 0, 1)"},
            {"Color.fromRGB(255, 255, 255, 51).a", "Color.fromRGB(255, 255, 255, 51).a", 0.2},
            {"Color.white().toString()", "Color.white():toString()", "Color(1, 1, 1, 1)"},
            {"Color.black().toString()", "Color.black():toString()", "Color(0, 0, 0, 1)"},
            {"Color.red().toString()", "Color.red():toString()", "Color(1, 0, 0, 1)"},
            {"Color.green().toString()", "Color.green():toString()", "Color(0, 1, 0, 1)"},
            {"Color.blue().toString()", "Color.blue():toString()", "Color(0, 0, 1, 1)"},
            {"Color.yellow().toString()", "Color.yellow():toString()", "Color(1, 1, 0, 1)"},
            {"Color.cyan().toString()", "Color.cyan():toString()", "Color(0, 1, 1, 1)"},
            {"Color.magenta().toString()", "Color.magenta():toString()", "Color(1, 0, 1, 1)"},
            {"Color.transparent().toString()", "Color.transparent():toString()", "Color(0, 0, 0, 0)"},
        };
    }

    // glm parameters, return values and properties of a bound class.
    inline std::vector<Case> GlmCases() {
        return {
            {"String(new Anchor().flip(new Vector2(1, 2)))", "tostring(Anchor.new():flip(Vector2.new(1, 2)))", "Vector2(2, 1)"},
            {"String(new Anchor().shift(new Vector3(1, 2, 3)))", "tostring(Anchor.new():shift(Vector3.new(1, 2, 3)))", "Vector3(1, 2, 3)"},
            {"String(new Anchor().scale(new Vector4(1, 2, 3, 4), 2))", "tostring(Anchor.new():scale(Vector4.new(1, 2, 3, 4), 2))", "Vector4(2, 4, 6, 8)"},
            {"String(new Anchor().turn(new Quaternion(0, 0, 1, 0)))", "tostring(Anchor.new():turn(Quaternion.new(0, 0, 1, 0)))", "Quaternion(0, 0, 1, 0)"},
            {"String(new Anchor().position)", "tostring(Anchor.new().position)", "Vector3(0, 0, 0)"},
            {"{ const a = new Anchor(); a.position = new Vector3(1, 2, 3); String(a.shift(new Vector3(1, 1, 1))) }", "local a = Anchor.new(); a.position = Vector3.new(1, 2, 3); return tostring(a:shift(Vector3.new(1, 1, 1)))", "Vector3(2, 3, 4)"},
            {"String(new Anchor().rotation)", "tostring(Anchor.new().rotation)", "Quaternion(1, 0, 0, 0)"},
            {"{ const a = new Anchor(); a.rotation = new Quaternion(0, 1, 0, 0); a.rotation.x }", "local a = Anchor.new(); a.rotation = Quaternion.new(0, 1, 0, 0); return a.rotation.x", 1},
            {"String(Anchor.origin())", "tostring(Anchor.origin())", "Vector3(0, 0, 0)"},
            // A value a method returns is a value type like any other.
            {"Anchor.origin().add(new Vector3(1, 2, 3)).length", "Anchor.origin():add(Vector3.new(1, 2, 3)).length", std::sqrt(14.0)},
        };
    }
} // namespace ValueTypesParityTest

MODULE(value_types_parity, {
    using namespace Framework::Scripting;
    using ValueTypesParityTest::AnchorDefinition;
    using ValueTypesParityTest::Runtimes;

    IT("gives Vector2 the same behaviour in JavaScript and in Lua", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);
        const std::string mismatches = runtimes.Mismatches(ValueTypesParityTest::Vector2Cases());
        EQUALS(mismatches, "");
    });

    IT("gives Vector3 the same behaviour in JavaScript and in Lua", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);
        const std::string mismatches = runtimes.Mismatches(ValueTypesParityTest::Vector3Cases());
        EQUALS(mismatches, "");
    });

    IT("gives Vector4 the same behaviour in JavaScript and in Lua", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);
        const std::string mismatches = runtimes.Mismatches(ValueTypesParityTest::Vector4Cases());
        EQUALS(mismatches, "");
    });

    IT("gives Quaternion the same behaviour in JavaScript and in Lua", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);
        const std::string mismatches = runtimes.Mismatches(ValueTypesParityTest::QuaternionCases());
        EQUALS(mismatches, "");
    });

    IT("gives Color the same behaviour in JavaScript and in Lua", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);
        const std::string mismatches = runtimes.Mismatches(ValueTypesParityTest::ColorCases());
        EQUALS(mismatches, "");
    });

    IT("keeps the lenient Color constructor in JavaScript and the declared one in Lua", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);

        // JavaScript: a missing or non-numeric component is 0, a missing alpha is 1.
        EQUALS(runtimes.Js("new Color().toString()"), "Color(0, 0, 0, 1)");
        EQUALS(runtimes.Js("new Color('red', 0.5).toString()"), "Color(0, 0.5, 0, 1)");

        // Lua: the three components are required and typed, alpha may be omitted or nil.
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Color.new(0.25, 0.5, 0.75, nil):toString()"), "Color(0.25, 0.5, 0.75, 1)");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Color.new()"), "<error> eval:1: wrong number of arguments to 'Color.new' (3..4 expected, got 0)");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Color.new('red', 0.5, 0)"), "<error> eval:1: bad argument #1 to 'Color.new' (number expected, got string)");
    });

    // Held against the classes as they were written by hand, before they were declared through the binding layer.
    IT("keeps the JavaScript classes as they were", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);

        // toJSON is what JSON.stringify calls. Lua has no JSON yet, so it has no counterpart there.
        EQUALS(runtimes.Js("JSON.stringify(new Vector2(1, 2))"), R"({"x":1,"y":2})");
        EQUALS(runtimes.Js("JSON.stringify(new Vector3(1, 2, 3))"), R"({"x":1,"y":2,"z":3})");
        EQUALS(runtimes.Js("JSON.stringify(new Vector4(1, 2, 3, 4))"), R"({"x":1,"y":2,"z":3,"w":4})");
        EQUALS(runtimes.Js("JSON.stringify(new Quaternion(1, 2, 3, 4))"), R"({"w":1,"x":2,"y":3,"z":4})");
        EQUALS(runtimes.Js("JSON.stringify(new Color(0.25, 0.5, 0.75))"), R"({"r":0.25,"g":0.5,"b":0.75,"a":1})");
        EQUALS(runtimes.Js("JSON.stringify({ at: new Vector3(1, 2, 3) })"), R"({"at":{"x":1,"y":2,"z":3}})");
        EQUALS(runtimes.Js("String(Object.getPrototypeOf(new Vector3(1, 2, 3)).toJSON.call({}))"), "undefined");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Vector2.new(1, 2).toJSON or Vector3.new(1, 2, 3).toJSON or Vector4.new(1, 2, 3, 4).toJSON or Quaternion.identity().toJSON or Color.red().toJSON"), "nil");

        // The functions of each constructor, then what the prototype of an instance holds.
        runtimes.Js("function surface(type, instance) {"
                    "    const statics = Object.getOwnPropertyNames(type).filter(name => typeof type[name] === 'function').sort();"
                    "    const members = Object.getOwnPropertyNames(Object.getPrototypeOf(instance)).sort();"
                    "    return statics.join(' ') + ' | ' + members.join(' ');"
                    "}");
        EQUALS(runtimes.Js("surface(Vector2, new Vector2(0, 0))"), "one zero | add clone constructor distance div dot length lengthSquared lerp mul normalize set sub toJSON toString x y");
        EQUALS(runtimes.Js("surface(Vector3, new Vector3(0, 0, 0))"), "forward one right up zero | add clone constructor cross distance div dot length lengthSquared lerp mul normalize set sub toJSON toString x y z");
        EQUALS(runtimes.Js("surface(Vector4, new Vector4(0, 0, 0, 0))"), "one zero | add clone constructor distance div dot length lengthSquared lerp mul normalize set sub toJSON toString w x y z");
        EQUALS(runtimes.Js("surface(Quaternion, new Quaternion(1, 0, 0, 0))"), "fromAxisAngle fromEuler identity | clone conjugate constructor dot inverse length lengthSquared mul normalize rotateVector set slerp toEuler toJSON toString w x y z");
        EQUALS(runtimes.Js("surface(Color, new Color(0, 0, 0))"), "black blue cyan fromHex fromRGB green magenta red transparent white yellow | a b clone constructor g lerp r set toHex toJSON toString");

        // A read-only property ignores an assignment, and the classes are constructed with or without new.
        EQUALS(runtimes.Js("{ const v = new Vector3(3, 4, 0); v.length = 1; v.length }"), "5");
        EQUALS(runtimes.Js("Vector3(1, 2, 3) instanceof Vector3"), "true");
        EQUALS(runtimes.Js("Quaternion.identity().rotateVector(new Vector3(1, 2, 3)) instanceof Vector3"), "true");

        // A wrong argument is refused in the words of v8pp.
        EQUALS(runtimes.Js("new Vector3(1, 2)"), "<threw> Error: Argument count does not match function definition. Expected 3 but got 2");
        EQUALS(runtimes.Js("new Vector3(1, 2, 3).add()"), "<threw> Error: Argument count does not match function definition. Expected 1 but got 0");
        EQUALS(runtimes.Js("new Vector3(1, 2, 3).add(5)"), "<threw> Error: expected Object, typeof=number");
        EQUALS(runtimes.Js("new Vector3(1, 2, 3).mul('2')"), "<threw> Error: expected Number, typeof=string");
        EQUALS(runtimes.Js("new Quaternion(1, 0, 0, 0).rotateVector(new Vector2(1, 2))"), "<threw> Error: failed to unwrap C++ object");
        EQUALS(runtimes.Js("Color.fromHex(5).toString()"), "Color(1, 1, 1, 1)");
        EQUALS(runtimes.Js("new Color(1, 0, 0).set(1, 1)"), "<threw> Error: Argument count does not match function definition. Expected 4 but got 2");
        EQUALS(runtimes.Js("new Color(1, 0, 0).toHex(1)"), "<threw> Error: expected Optional, typeof=number");
    });

    IT("passes glm values as the value types in both languages", {
        Runtimes runtimes;
        EQUALS(runtimes.Started(), true);
        const std::string mismatches = runtimes.Mismatches(ValueTypesParityTest::GlmCases());
        EQUALS(mismatches, "");

        const Bind::ClassDesc &anchor = AnchorDefinition().Describe();
        EQUALS(Bind::ToTypeScript(anchor.functions[0].params[0].type), "Vector2");
        EQUALS(Bind::ToLuaAnnotation(anchor.functions[1].returns), "Vector3");
        EQUALS(Bind::ToLuaAnnotation(anchor.functions[2].params[0].type), "Vector4");
        EQUALS(Bind::ToTypeScript(anchor.properties[1].type), "Quaternion");

        // JavaScript takes any object with the components. Lua takes the value type only.
        EQUALS(runtimes.Js("String(new Anchor().shift({ x: 1, y: 2, z: 3 }))"), "Vector3(1, 2, 3)");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Anchor.new():shift({ x = 1, y = 2, z = 3 })"), "<error> eval:1: bad argument #1 to 'Anchor:shift' (Vector3 expected, got table)");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Anchor.new():shift(Vector4.new(1, 2, 3, 4))"), "<error> eval:1: bad argument #1 to 'Anchor:shift' (Vector3 expected, got Vector4)");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "Anchor.new():turn(Vector4.new(1, 2, 3, 4))"), "<error> eval:1: bad argument #1 to 'Anchor:turn' (Quaternion expected, got Vector4)");
        EQUALS(LuaTest::Eval(runtimes.Lua(), "local a = Anchor.new(); a.position = 5"), "<error> eval:1: bad argument #1 to 'Anchor.position' (Vector3 expected, got number)");
    });

    IT("converts glm values on the Lua stack through the value types", {
        LuaTest::State state;
        lua_State *L = state.L;
        Builtins::RegisterValueTypesLua(L, Bind::Side::Server);

        Bind::LuaConvert<glm::vec2>::Push(L, glm::vec2(1.0f, 2.0f));
        Bind::LuaConvert<glm::vec3>::Push(L, glm::vec3(1.0f, 2.0f, 3.0f));
        Bind::LuaConvert<glm::vec4>::Push(L, glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
        Bind::LuaConvert<glm::quat>::Push(L, glm::quat(1.0f, 2.0f, 3.0f, 4.0f));
        EQUALS(lua_gettop(L), 4);

        STREQUALS(Bind::LuaTypeNameAt(L, 1), "Vector2");
        STREQUALS(Bind::LuaTypeNameAt(L, 2), "Vector3");
        STREQUALS(Bind::LuaTypeNameAt(L, 3), "Vector4");
        STREQUALS(Bind::LuaTypeNameAt(L, 4), "Quaternion");

        EQUALS(Bind::LuaConvert<glm::vec2>::Is(L, 1), true);
        EQUALS(Bind::LuaConvert<glm::vec3>::Is(L, 2), true);
        EQUALS(Bind::LuaConvert<glm::vec4>::Is(L, 3), true);
        EQUALS(Bind::LuaConvert<glm::quat>::Is(L, 4), true);
        EQUALS(Bind::LuaConvert<glm::vec2>::Get(L, 1) == glm::vec2(1.0f, 2.0f), true);
        EQUALS(Bind::LuaConvert<glm::vec3>::Get(L, 2) == glm::vec3(1.0f, 2.0f, 3.0f), true);
        EQUALS(Bind::LuaConvert<glm::vec4>::Get(L, 3) == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f), true);
        EQUALS(Bind::LuaConvert<glm::quat>::Get(L, 4) == glm::quat(1.0f, 2.0f, 3.0f, 4.0f), true);

        // A value type is not another one, and a Color is not the Vector4 it holds.
        EQUALS(Bind::LuaConvert<glm::vec3>::Is(L, 1), false);
        EQUALS(Bind::LuaConvert<glm::vec4>::Is(L, 4), false);
        EQUALS(Bind::LuaConvert<glm::quat>::Is(L, 3), false);
        Bind::LuaConvert<Builtins::Color>::Push(L, Builtins::Color(1.0f, 0.0f, 0.0f));
        EQUALS(Bind::LuaConvert<glm::vec4>::Is(L, -1), false);

        // Nor is a table with the components, a number or nothing.
        lua_newtable(L);
        lua_pushnumber(L, 1.0);
        lua_setfield(L, -2, "x");
        EQUALS(Bind::LuaConvert<glm::vec3>::Is(L, -1), false);
        lua_pushnumber(L, 1.0);
        EQUALS(Bind::LuaConvert<glm::vec2>::Is(L, -1), false);
        EQUALS(Bind::LuaConvert<glm::quat>::Is(L, 20), false);

        STREQUALS(Bind::LuaConvert<glm::vec2>::Expected(), "Vector2");
        STREQUALS(Bind::LuaConvert<glm::vec3>::Expected(), "Vector3");
        STREQUALS(Bind::LuaConvert<glm::vec4>::Expected(), "Vector4");
        STREQUALS(Bind::LuaConvert<glm::quat>::Expected(), "Quaternion");
    });

    IT("adds operators to the vectors in Lua, each giving a new value", {
        LuaTest::State state;
        lua_State *L = state.L;
        Builtins::RegisterValueTypesLua(L, Bind::Side::Server);

        EQUALS(LuaTest::Eval(L, "tostring(Vector2.new(1, 2) + Vector2.new(3, 5))"), "Vector2(4, 7)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector2.new(1, 2) - Vector2.new(3, 5))"), "Vector2(-2, -3)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector2.new(1, 2) * 2.5)"), "Vector2(2.5, 5)");
        EQUALS(LuaTest::Eval(L, "tostring(2.5 * Vector2.new(1, 2))"), "Vector2(2.5, 5)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector2.new(1, 2) / 4)"), "Vector2(0.25, 0.5)");
        EQUALS(LuaTest::Eval(L, "tostring(-Vector2.new(1, -2))"), "Vector2(-1, 2)");
        EQUALS(LuaTest::Eval(L, "Vector2.new(1, 2) == Vector2.new(1, 2)"), "true");
        EQUALS(LuaTest::Eval(L, "Vector2.new(1, 2) == Vector2.new(1, 3)"), "false");
        EQUALS(LuaTest::Eval(L, "Vector2.new(1, 2) ~= Vector2.new(1, 3)"), "true");

        EQUALS(LuaTest::Eval(L, "tostring(Vector3.new(1, 2, 3) + Vector3.new(4, 5, 6))"), "Vector3(5, 7, 9)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector3.new(1, 2, 3) - Vector3.new(4, 6, 8))"), "Vector3(-3, -4, -5)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector3.new(1, 2, 3) * 2)"), "Vector3(2, 4, 6)");
        EQUALS(LuaTest::Eval(L, "tostring(2 * Vector3.new(1, 2, 3))"), "Vector3(2, 4, 6)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector3.new(1, 2, 3) / 4)"), "Vector3(0.25, 0.5, 0.75)");
        EQUALS(LuaTest::Eval(L, "tostring(-Vector3.new(1, -2, 3))"), "Vector3(-1, 2, -3)");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) == Vector3.new(1, 2, 3)"), "true");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) == Vector3.new(1, 2, 4)"), "false");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) ~= Vector3.new(1, 2, 3)"), "false");

        EQUALS(LuaTest::Eval(L, "tostring(Vector4.new(1, 2, 3, 4) + Vector4.new(4, 5, 6, 7))"), "Vector4(5, 7, 9, 11)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector4.new(1, 2, 3, 4) - Vector4.new(4, 6, 8, 10))"), "Vector4(-3, -4, -5, -6)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector4.new(1, 2, 3, 4) * 2)"), "Vector4(2, 4, 6, 8)");
        EQUALS(LuaTest::Eval(L, "tostring(2 * Vector4.new(1, 2, 3, 4))"), "Vector4(2, 4, 6, 8)");
        EQUALS(LuaTest::Eval(L, "tostring(Vector4.new(1, 2, 3, 4) / 4)"), "Vector4(0.25, 0.5, 0.75, 1)");
        EQUALS(LuaTest::Eval(L, "tostring(-Vector4.new(1, -2, 3, -4))"), "Vector4(-1, 2, -3, 4)");
        EQUALS(LuaTest::Eval(L, "Vector4.new(1, 2, 3, 4) == Vector4.new(1, 2, 3, 4)"), "true");
        EQUALS(LuaTest::Eval(L, "Vector4.new(1, 2, 3, 4) == Vector4.new(1, 2, 3, 5)"), "false");

        // The operands are left as they were, and the result is neither of them.
        EQUALS(LuaTest::Eval(L, "local a, b = Vector3.new(1, 2, 3), Vector3.new(4, 5, 6); local c = (a + b) * 2 - a / 1; c = -c; return tostring(a) .. ' ' .. tostring(b)"), "Vector3(1, 2, 3) Vector3(4, 5, 6)");
        EQUALS(LuaTest::Eval(L, "local a = Vector3.new(1, 2, 3); return rawequal(a + Vector3.zero(), a) or rawequal(a * 1, a) or rawequal(a / 1, a) or rawequal(-a, a)"), "false");
        EQUALS(LuaTest::Eval(L, "tostring((Vector3.new(1, 2, 3) + Vector3.one()):mul(2))"), "Vector3(4, 6, 8)");

        // Equality compares the components exactly, and only within one type.
        EQUALS(LuaTest::Eval(L, "Vector3.new(0.1, 0.2, 0.3) == Vector3.new(0.1, 0.2, 0.3 + 1e-6)"), "false");
        EQUALS(LuaTest::Eval(L, "Vector2.new(1, 2) == Vector3.new(1, 2, 0)"), "false");
        EQUALS(LuaTest::Eval(L, "Vector4.new(1, 0, 0, 1) == Color.new(1, 0, 0, 1)"), "false");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) == 5"), "false");

        // An operand the operator does not take is an error that names the operator.
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) + 1"), "<error> eval:1: bad argument #2 to 'Vector3.__add' (Vector3 expected, got number)");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) - Vector2.new(1, 2)"), "<error> eval:1: bad argument #2 to 'Vector3.__sub' (Vector3 expected, got Vector2)");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3) * Vector3.new(1, 2, 3)"), "<error> eval:1: bad argument #2 to 'Vector3.__mul' (number expected, got Vector3)");
        EQUALS(LuaTest::Eval(L, "2 / Vector3.new(1, 2, 3)"), "<error> eval:1: bad argument #1 to 'Vector3.__div' (Vector3 expected, got number)");
        EQUALS(LuaTest::Eval(L, "Vector2.new(1, 2) < Vector2.new(3, 4)"), "<error> eval:1: attempt to compare two Vector2 values");
    });

    IT("adds composition to Quaternion and equality to Quaternion and Color in Lua", {
        LuaTest::State state;
        lua_State *L = state.L;
        Builtins::RegisterValueTypesLua(L, Bind::Side::Server);

        EQUALS(LuaTest::EvalNumber(L, "(Quaternion.new(0, 1, 0, 0) * Quaternion.new(0, 0, 1, 0)).z"), 1.0);
        EQUALS(LuaTest::EvalNumber(L, "(Quaternion.new(0, 0, 1, 0) * Quaternion.new(0, 1, 0, 0)).z"), -1.0);
        EQUALS(LuaTest::Eval(L, "local a, b = Quaternion.new(0, 1, 0, 0), Quaternion.new(0, 0, 1, 0); local c = a * b; return tostring(a) .. ' ' .. tostring(b) .. ' ' .. tostring(rawequal(c, a))"), "Quaternion(0, 1, 0, 0) Quaternion(0, 0, 1, 0) false");
        EQUALS(LuaTest::Eval(L, "Quaternion.new(0, 1, 0, 0) * Quaternion.new(0, 0, 1, 0) == Quaternion.new(0, 1, 0, 0):mul(Quaternion.new(0, 0, 1, 0))"), "true");
        EQUALS(LuaTest::Eval(L, "Quaternion.identity() == Quaternion.new(1, 0, 0, 0)"), "true");
        EQUALS(LuaTest::Eval(L, "Quaternion.identity() == Quaternion.new(1, 0, 0, 0.5)"), "false");
        EQUALS(LuaTest::Eval(L, "Quaternion.identity() == Vector4.new(0, 0, 0, 1)"), "false");
        EQUALS(LuaTest::Eval(L, "Quaternion.identity() * 2"), "<error> eval:1: bad argument #2 to 'Quaternion.__mul' (Quaternion expected, got number)");
        EQUALS(LuaTest::Eval(L, "Quaternion.identity() + Quaternion.identity()"), "<error> eval:1: attempt to perform arithmetic on a Quaternion value");

        EQUALS(LuaTest::Eval(L, "Color.red() == Color.new(1, 0, 0)"), "true");
        EQUALS(LuaTest::Eval(L, "Color.red() == Color.new(1, 0, 0, 0.5)"), "false");
        EQUALS(LuaTest::Eval(L, "Color.red() ~= Color.blue()"), "true");
        EQUALS(LuaTest::Eval(L, "Color.red() + Color.blue()"), "<error> eval:1: attempt to perform arithmetic on a Color value");
    });

    IT("registers the value types in Lua as read-only globals with typed members", {
        LuaTest::State state;
        lua_State *L = state.L;
        Builtins::RegisterValueTypesLua(L, Bind::Side::Server);
        EQUALS(lua_gettop(L), 0);

        EQUALS(LuaTest::Eval(L, "type(Vector2) .. type(Vector3) .. type(Vector4) .. type(Quaternion) .. type(Color)"), "tabletabletabletabletable");
        EQUALS(LuaTest::Eval(L, "type(Vector3.new(1, 2, 3))"), "userdata");

        // Registering again changes nothing.
        Builtins::RegisterValueTypesLua(L, Bind::Side::Server);
        EQUALS(lua_gettop(L), 0);
        EQUALS(LuaTest::Eval(L, "tostring(Vector3.one())"), "Vector3(1, 1, 1)");

        EQUALS(LuaTest::Eval(L, "local v = Vector3.new(3, 4, 0); v.length = 1"), "<error> eval:1: attempt to set 'length' on Vector3");
        EQUALS(LuaTest::Eval(L, "Vector3.zero = nil"), "<error> eval:1: attempt to set 'zero' on class Vector3");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2)"), "<error> eval:1: wrong number of arguments to 'Vector3.new' (3..3 expected, got 2)");
        EQUALS(LuaTest::Eval(L, "Vector3.new(1, 2, 3):add(1)"), "<error> eval:1: bad argument #1 to 'Vector3:add' (Vector3 expected, got number)");
        EQUALS(LuaTest::Eval(L, "Quaternion.fromAxisAngle(Vector2.new(0, 1), 1)"), "<error> eval:1: bad argument #1 to 'Quaternion.fromAxisAngle' (Vector3 expected, got Vector2)");
        EQUALS(LuaTest::Eval(L, "Color.fromHex(255)"), "<error> eval:1: bad argument #1 to 'Color.fromHex' (string expected, got number)");
    });

    IT("describes each value type once for both languages", {
        const Bind::ClassDesc &vector3 = Builtins::Vector3::Definition().Describe();
        EQUALS(vector3.name, "Vector3");
        EQUALS(vector3.description, "Mutable three-dimensional vector for positions, directions, and Euler angles.");
        EQUALS(vector3.constructor.has_value(), true);
        EQUALS(vector3.constructor->params.size(), 3u);
        EQUALS(vector3.functions.size(), 17u);
        EQUALS(vector3.properties.size(), 5u);
        EQUALS(Bind::ToLuaAnnotation(vector3.functions[0].returns), "self");
        EQUALS(Bind::ToTypeScript(vector3.functions[0].returns), "this");

        EQUALS(Bind::ApiCatalog::Get().Find("Vector2") == &Builtins::Vector2::Definition().Describe(), true);
        EQUALS(Bind::ApiCatalog::Get().Find("Vector3") == &vector3, true);
        EQUALS(Bind::ApiCatalog::Get().Find("Vector4") == &Builtins::Vector4::Definition().Describe(), true);
        EQUALS(Bind::ApiCatalog::Get().Find("Quaternion") == &Builtins::Quaternion::Definition().Describe(), true);
        EQUALS(Bind::ApiCatalog::Get().Find("Color") == &Builtins::Color::Definition().Describe(), true);

        // The alpha a script may omit is optional in the description both languages are documented from.
        const Bind::ClassDesc &color = Builtins::Color::Definition().Describe();
        EQUALS(color.constructor->params[3].optional, true);
        EQUALS(color.constructor->params[2].optional, false);
    });
})
