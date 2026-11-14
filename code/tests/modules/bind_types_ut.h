/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/bind/descriptors.h"
#include "scripting/bind/doc.h"
#include "scripting/bind/types.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace BindTypesTest {

    enum class Mode : uint8_t {
        Off,
        On
    };

    class Widget final {
      public:
        Widget &grow(float amount) {
            _size += amount;
            return *this;
        }

        Widget &reset() noexcept {
            _size = 0.0f;
            return *this;
        }

        float scaled(float factor, std::optional<int> steps) const {
            return _size * factor * static_cast<float>(steps.value_or(1));
        }

        Widget clone() const {
            return *this;
        }

        const Widget &view() const {
            return *this;
        }

        std::string toString() const noexcept {
            return "Widget";
        }

        void clear() {
            _size = 0.0f;
        }

        static Widget make(int) {
            return {};
        }

      private:
        float _size = 0.0f;
    };

    inline float Distance(const glm::vec3 &a, const glm::vec3 &b) {
        return glm::distance(a, b);
    }

    inline Widget &Same(Widget &widget) {
        return widget;
    }

    template <typename T>
    std::string TypeScript() {
        return Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::TypeOf<T>());
    }

    template <typename T>
    std::string Lua() {
        return Framework::Scripting::Bind::ToLuaAnnotation(Framework::Scripting::Bind::TypeOf<T>());
    }

    // The message of the std::logic_error `action` throws, or empty when it does not throw one.
    template <typename Action>
    std::string LogicErrorOf(Action action) {
        try {
            action();
        }
        catch (const std::logic_error &error) {
            return error.what();
        }
        return {};
    }
} // namespace BindTypesTest

FW_SCRIPT_CLASS(BindTypesTest::Widget, "Widget");

MODULE(bind_types, {

    // --- TypeOf and both spellings, one test per row of the type model ---

    IT("describes bool as Boolean", {
        EQUALS(Framework::Scripting::Bind::TypeOf<bool>().kind, Framework::Scripting::Bind::TypeKind::Boolean);
        EQUALS(Framework::Scripting::Bind::TypeOf<const bool &>().kind, Framework::Scripting::Bind::TypeKind::Boolean);
        EQUALS(BindTypesTest::TypeScript<bool>(), "boolean");
        EQUALS(BindTypesTest::Lua<bool>(), "boolean");
    });

    IT("describes float and double as Number", {
        EQUALS(Framework::Scripting::Bind::TypeOf<float>().kind, Framework::Scripting::Bind::TypeKind::Number);
        EQUALS(Framework::Scripting::Bind::TypeOf<double>().kind, Framework::Scripting::Bind::TypeKind::Number);
        EQUALS(Framework::Scripting::Bind::TypeOf<const double &>().kind, Framework::Scripting::Bind::TypeKind::Number);
        EQUALS(BindTypesTest::TypeScript<float>(), "number");
        EQUALS(BindTypesTest::Lua<float>(), "number");
        EQUALS(BindTypesTest::TypeScript<double>(), "number");
        EQUALS(BindTypesTest::Lua<double>(), "number");
    });

    IT("describes integral and enum types as Integer", {
        EQUALS(Framework::Scripting::Bind::TypeOf<int>().kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::TypeOf<uint8_t>().kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::TypeOf<int64_t>().kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::TypeOf<uint64_t>().kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::TypeOf<const size_t &>().kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::TypeOf<BindTypesTest::Mode>().kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(BindTypesTest::TypeScript<int>(), "number");
        EQUALS(BindTypesTest::Lua<int>(), "integer");
        EQUALS(BindTypesTest::TypeScript<uint64_t>(), "number");
        EQUALS(BindTypesTest::Lua<uint64_t>(), "integer");
        EQUALS(BindTypesTest::TypeScript<BindTypesTest::Mode>(), "number");
        EQUALS(BindTypesTest::Lua<BindTypesTest::Mode>(), "integer");
    });

    IT("describes std::string, std::string_view and const char * as String", {
        EQUALS(Framework::Scripting::Bind::TypeOf<std::string>().kind, Framework::Scripting::Bind::TypeKind::String);
        EQUALS(Framework::Scripting::Bind::TypeOf<const std::string &>().kind, Framework::Scripting::Bind::TypeKind::String);
        EQUALS(Framework::Scripting::Bind::TypeOf<std::string_view>().kind, Framework::Scripting::Bind::TypeKind::String);
        EQUALS(Framework::Scripting::Bind::TypeOf<const char *>().kind, Framework::Scripting::Bind::TypeKind::String);
        EQUALS(BindTypesTest::TypeScript<std::string>(), "string");
        EQUALS(BindTypesTest::Lua<std::string>(), "string");
        EQUALS(BindTypesTest::TypeScript<std::string_view>(), "string");
        EQUALS(BindTypesTest::Lua<std::string_view>(), "string");
        EQUALS(BindTypesTest::TypeScript<const char *>(), "string");
        EQUALS(BindTypesTest::Lua<const char *>(), "string");
    });

    IT("describes std::optional<T> as Optional(T)", {
        const Framework::Scripting::Bind::TypeRef type = Framework::Scripting::Bind::TypeOf<std::optional<int>>();
        EQUALS(type.kind, Framework::Scripting::Bind::TypeKind::Optional);
        EQUALS(type.args.size(), 1u);
        EQUALS(type.args[0].kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::TypeOf<const std::optional<int> &>().kind, Framework::Scripting::Bind::TypeKind::Optional);
        EQUALS(BindTypesTest::TypeScript<std::optional<int>>(), "number | undefined");
        EQUALS(BindTypesTest::Lua<std::optional<int>>(), "integer?");
        EQUALS(BindTypesTest::TypeScript<std::optional<std::string>>(), "string | undefined");
        EQUALS(BindTypesTest::Lua<std::optional<std::string>>(), "string?");
    });

    IT("describes std::vector<T> as Array(T)", {
        const Framework::Scripting::Bind::TypeRef type = Framework::Scripting::Bind::TypeOf<std::vector<std::string>>();
        EQUALS(type.kind, Framework::Scripting::Bind::TypeKind::Array);
        EQUALS(type.args.size(), 1u);
        EQUALS(type.args[0].kind, Framework::Scripting::Bind::TypeKind::String);
        EQUALS(Framework::Scripting::Bind::TypeOf<const std::vector<float> &>().kind, Framework::Scripting::Bind::TypeKind::Array);
        EQUALS(BindTypesTest::TypeScript<std::vector<std::string>>(), "string[]");
        EQUALS(BindTypesTest::Lua<std::vector<std::string>>(), "string[]");
        EQUALS(BindTypesTest::TypeScript<std::vector<std::vector<float>>>(), "number[][]");
        EQUALS(BindTypesTest::Lua<std::vector<std::vector<float>>>(), "number[][]");
        EQUALS(BindTypesTest::TypeScript<std::optional<std::vector<BindTypesTest::Widget>>>(), "Widget[] | undefined");
        EQUALS(BindTypesTest::Lua<std::optional<std::vector<BindTypesTest::Widget>>>(), "Widget[]?");
    });

    IT("keeps an optional element apart from the array brackets", {
        EQUALS(BindTypesTest::TypeScript<std::vector<std::optional<int>>>(), "(number | undefined)[]");
        EQUALS(BindTypesTest::Lua<std::vector<std::optional<int>>>(), "(integer?)[]");
    });

    IT("describes a bound class by its script name, through references and pointers", {
        EQUALS(Framework::Scripting::Bind::TypeOf<BindTypesTest::Widget>().kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(Framework::Scripting::Bind::TypeOf<BindTypesTest::Widget>().name, "Widget");
        EQUALS(Framework::Scripting::Bind::TypeOf<BindTypesTest::Widget &>().name, "Widget");
        EQUALS(Framework::Scripting::Bind::TypeOf<const BindTypesTest::Widget &>().name, "Widget");
        EQUALS(Framework::Scripting::Bind::TypeOf<BindTypesTest::Widget *>().name, "Widget");
        EQUALS(Framework::Scripting::Bind::TypeOf<const BindTypesTest::Widget *>().kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(BindTypesTest::TypeScript<const BindTypesTest::Widget &>(), "Widget");
        EQUALS(BindTypesTest::Lua<const BindTypesTest::Widget &>(), "Widget");
        EQUALS(BindTypesTest::TypeScript<BindTypesTest::Widget *>(), "Widget");
        EQUALS(BindTypesTest::Lua<BindTypesTest::Widget *>(), "Widget");
    });

    IT("describes glm vectors and quaternions as the value classes", {
        EQUALS(Framework::Scripting::Bind::TypeOf<glm::vec2>().kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(Framework::Scripting::Bind::TypeOf<glm::vec2>().name, "Vector2");
        EQUALS(Framework::Scripting::Bind::TypeOf<glm::vec3>().name, "Vector3");
        EQUALS(Framework::Scripting::Bind::TypeOf<const glm::vec3 &>().name, "Vector3");
        EQUALS(Framework::Scripting::Bind::TypeOf<glm::vec4>().name, "Vector4");
        EQUALS(Framework::Scripting::Bind::TypeOf<glm::quat>().name, "Quaternion");
        EQUALS(BindTypesTest::TypeScript<glm::vec2>(), "Vector2");
        EQUALS(BindTypesTest::Lua<glm::vec2>(), "Vector2");
        EQUALS(BindTypesTest::TypeScript<glm::vec3>(), "Vector3");
        EQUALS(BindTypesTest::Lua<glm::vec3>(), "Vector3");
        EQUALS(BindTypesTest::TypeScript<glm::vec4>(), "Vector4");
        EQUALS(BindTypesTest::Lua<glm::vec4>(), "Vector4");
        EQUALS(BindTypesTest::TypeScript<glm::quat>(), "Quaternion");
        EQUALS(BindTypesTest::Lua<glm::quat>(), "Quaternion");
    });

    IT("describes a method returning a reference to its own class as This", {
        const Framework::Scripting::Bind::FunctionDesc grow = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::grow)>("grow", Framework::Scripting::Bind::Doc("Grows.").Param("amount", "Size to add."), false, Framework::Scripting::Bind::Side::Shared);
        EQUALS(grow.returns.kind, Framework::Scripting::Bind::TypeKind::This);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(grow.returns), "this");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(grow.returns), "self");

        const Framework::Scripting::Bind::FunctionDesc reset = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::reset)>("reset", Framework::Scripting::Bind::Doc("Resets."), false, Framework::Scripting::Bind::Side::Shared);
        EQUALS(reset.returns.kind, Framework::Scripting::Bind::TypeKind::This);

        EQUALS(Framework::Scripting::Bind::Type::This().kind, Framework::Scripting::Bind::TypeKind::This);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::Type::This()), "this");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(Framework::Scripting::Bind::Type::This()), "self");
    });

    IT("describes void as Void", {
        EQUALS(Framework::Scripting::Bind::TypeOf<void>().kind, Framework::Scripting::Bind::TypeKind::Void);
        EQUALS(Framework::Scripting::Bind::TypeRef {}.kind, Framework::Scripting::Bind::TypeKind::Void);
        EQUALS(BindTypesTest::TypeScript<void>(), "void");
        EQUALS(BindTypesTest::Lua<void>(), "nil");
    });

    IT("spells Any, a class and a custom type as given", {
        EQUALS(Framework::Scripting::Bind::Type::Any().kind, Framework::Scripting::Bind::TypeKind::Any);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::Type::Any()), "any");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(Framework::Scripting::Bind::Type::Any()), "any");

        EQUALS(Framework::Scripting::Bind::Type::Class("Player").kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::Type::Class("Player")), "Player");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(Framework::Scripting::Bind::Type::Class("Player")), "Player");

        const Framework::Scripting::Bind::TypeRef custom = Framework::Scripting::Bind::Type::Custom("{ x: number; y: number }", "{ x: number, y: number }");
        EQUALS(custom.kind, Framework::Scripting::Bind::TypeKind::Custom);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(custom), "{ x: number; y: number }");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(custom), "{ x: number, y: number }");
    });

    // --- DescribeFunction ---

    IT("takes parameter types from the signature and names from the Doc, in order", {
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Scales the widget.").Param("factor", "Multiplier.").Param("steps", "Times to apply it.").Returns("The scaled size.");
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::scaled)>("scaled", doc, false, Framework::Scripting::Bind::Side::Server);

        EQUALS(fn.name, "scaled");
        EQUALS(fn.description, "Scales the widget.");
        EQUALS(fn.returnsDescription, "The scaled size.");
        EQUALS(fn.returns.kind, Framework::Scripting::Bind::TypeKind::Number);
        EQUALS(fn.isStatic, false);
        EQUALS(fn.side, Framework::Scripting::Bind::Side::Server);

        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[0].name, "factor");
        EQUALS(fn.params[0].description, "Multiplier.");
        EQUALS(fn.params[0].type.kind, Framework::Scripting::Bind::TypeKind::Number);
        EQUALS(fn.params[0].optional, false);
        EQUALS(fn.params[1].name, "steps");
        EQUALS(fn.params[1].description, "Times to apply it.");
    });

    IT("flags a std::optional parameter optional and unwraps its type", {
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Scales the widget.").Param("factor", "Multiplier.").Param("steps", "Times to apply it.");
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::scaled)>("scaled", doc, false, Framework::Scripting::Bind::Side::Shared);

        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[1].optional, true);
        EQUALS(fn.params[1].type.kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(fn.params[1].type), "number");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(fn.params[1].type), "integer");
    });

    IT("excludes the receiver of a member function, const and noexcept included", {
        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::clone)>("clone", Framework::Scripting::Bind::Doc("Copies."), false, Framework::Scripting::Bind::Side::Shared).params.size(), 0u);
        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::toString)>("toString", Framework::Scripting::Bind::Doc("Formats."), false, Framework::Scripting::Bind::Side::Shared).params.size(), 0u);
        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::clear)>("clear", Framework::Scripting::Bind::Doc("Clears."), false, Framework::Scripting::Bind::Side::Shared).returns.kind, Framework::Scripting::Bind::TypeKind::Void);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::clone)>("clone", Framework::Scripting::Bind::Doc("Copies."), false, Framework::Scripting::Bind::Side::Shared).returns), "Widget");
        EQUALS(Framework::Scripting::Bind::ToTypeScript(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::toString)>("toString", Framework::Scripting::Bind::Doc("Formats."), false, Framework::Scripting::Bind::Side::Shared).returns), "string");
    });

    IT("returns This only for a non-const reference to the method's own class", {
        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::view)>("view", Framework::Scripting::Bind::Doc("Views."), false, Framework::Scripting::Bind::Side::Shared).returns.kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::clone)>("clone", Framework::Scripting::Bind::Doc("Copies."), false, Framework::Scripting::Bind::Side::Shared).returns.kind, Framework::Scripting::Bind::TypeKind::Class);

        const Framework::Scripting::Bind::FunctionDesc same = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Same)>("same", Framework::Scripting::Bind::Doc("Returns it.").Param("widget", "Any widget."), true, Framework::Scripting::Bind::Side::Shared);
        EQUALS(same.returns.kind, Framework::Scripting::Bind::TypeKind::Class);
        EQUALS(same.returns.name, "Widget");
    });

    IT("takes every parameter of a free function", {
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Measures.").Param("a", "First point.").Param("b", "Second point.");
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Distance)>("distance", doc, true, Framework::Scripting::Bind::Side::Client);

        EQUALS(fn.isStatic, true);
        EQUALS(fn.side, Framework::Scripting::Bind::Side::Client);
        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[0].type.name, "Vector3");
        EQUALS(fn.params[1].name, "b");
        EQUALS(fn.returns.kind, Framework::Scripting::Bind::TypeKind::Number);

        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(BindTypesTest::Distance)>("distance", doc, true, Framework::Scripting::Bind::Side::Shared).params.size(), 2u);
        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::make)>("make", Framework::Scripting::Bind::Doc("Makes.").Param("size", "Initial size."), true, Framework::Scripting::Bind::Side::Shared).returns.name, "Widget");
    });

    IT("takes every parameter of a lambda", {
        const auto lambda = [](const BindTypesTest::Widget &, int count, const std::string &) {
            return count > 0;
        };
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Checks.").Param("widget", "Widget.").Param("count", "Count.").Param("label", "Label.");
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(lambda)>("check", doc, true, Framework::Scripting::Bind::Side::Shared);

        EQUALS(fn.params.size(), 3u);
        EQUALS(fn.params[0].type.name, "Widget");
        EQUALS(fn.params[1].type.kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(fn.params[2].type.kind, Framework::Scripting::Bind::TypeKind::String);
        EQUALS(fn.returns.kind, Framework::Scripting::Bind::TypeKind::Boolean);

        EQUALS(Framework::Scripting::Bind::DescribeFunction<decltype(+lambda)>("check", doc, true, Framework::Scripting::Bind::Side::Shared).params.size(), 3u);
    });

    IT("throws a logic_error naming the function when the Doc names another number of parameters", {
        const std::string tooFew = BindTypesTest::LogicErrorOf([] {
            Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::scaled)>("scaled", Framework::Scripting::Bind::Doc("Scales.").Param("factor", "Multiplier."), false, Framework::Scripting::Bind::Side::Shared);
        });
        EQUALS(tooFew.find("'scaled'") != std::string::npos, true);

        const std::string tooMany = BindTypesTest::LogicErrorOf([] {
            Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::clone)>("clone", Framework::Scripting::Bind::Doc("Copies.").Param("extra", "Not a parameter."), false, Framework::Scripting::Bind::Side::Shared);
        });
        EQUALS(tooMany.find("'clone'") != std::string::npos, true);

        const std::string constructor = BindTypesTest::LogicErrorOf([] {
            Framework::Scripting::Bind::DescribeConstructor<float, float>(Framework::Scripting::Bind::Doc("Creates.").Param("x", "X."));
        });
        EQUALS(constructor.find("'constructor'") != std::string::npos, true);
    });

    IT("lets ParamType override the type of the last parameter", {
        const auto lambda                                 = [](int, const BindTypesTest::Widget &) {};
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Moves.").Param("id", "Identifier.").Param("target", "Destination.").ParamType(Framework::Scripting::Bind::Type::Custom("{ x: number }", "{ x: number }"));
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(lambda)>("move", doc, true, Framework::Scripting::Bind::Side::Shared);

        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[0].type.kind, Framework::Scripting::Bind::TypeKind::Integer);
        EQUALS(fn.params[1].type.kind, Framework::Scripting::Bind::TypeKind::Custom);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(fn.params[1].type), "{ x: number }");
        EQUALS(Framework::Scripting::Bind::ToLuaAnnotation(fn.params[1].type), "{ x: number }");
        EQUALS(fn.params[1].name, "target");
    });

    IT("refuses ParamType before any parameter", {
        const std::string message = BindTypesTest::LogicErrorOf([] {
            Framework::Scripting::Bind::Doc("Moves.").ParamType(Framework::Scripting::Bind::Type::Any());
        });
        EQUALS(message.empty(), false);
    });

    IT("lets OptionalParam document a parameter optional whatever its C++ type", {
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Grows.").OptionalParam("amount", "Size to add; one when omitted.");
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::grow)>("grow", doc, false, Framework::Scripting::Bind::Side::Shared);

        EQUALS(fn.params.size(), 1u);
        EQUALS(fn.params[0].optional, true);
        EQUALS(fn.params[0].type.kind, Framework::Scripting::Bind::TypeKind::Number);
        EQUALS(fn.params[0].name, "amount");
        EQUALS(fn.params[0].description, "Size to add; one when omitted.");
    });

    IT("lets ReturnType override the derived return type", {
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Formats.").Returns("Plain object.").ReturnType(Framework::Scripting::Bind::Type::Custom("{ size: number }", "{ size: number }"));
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeFunction<decltype(&BindTypesTest::Widget::toString)>("toJSON", doc, false, Framework::Scripting::Bind::Side::Shared);

        EQUALS(fn.returns.kind, Framework::Scripting::Bind::TypeKind::Custom);
        EQUALS(Framework::Scripting::Bind::ToTypeScript(fn.returns), "{ size: number }");
        EQUALS(fn.returnsDescription, "Plain object.");
    });

    IT("describes a constructor from its argument types", {
        const Framework::Scripting::Bind::Doc doc         = Framework::Scripting::Bind::Doc("Creates a vector.").Param("x", "Initial X.").Param("y", "Initial Y.").Param("z", "Initial Z.");
        const Framework::Scripting::Bind::FunctionDesc fn = Framework::Scripting::Bind::DescribeConstructor<float, float, float>(doc);

        EQUALS(fn.name, "constructor");
        EQUALS(fn.description, "Creates a vector.");
        EQUALS(fn.params.size(), 3u);
        EQUALS(fn.params[2].name, "z");
        EQUALS(Framework::Scripting::Bind::ToTypeScript(fn.params[2].type), "number");
        EQUALS(Framework::Scripting::Bind::ToTypeScript(fn.returns), "void");
        EQUALS(fn.isStatic, false);
        EQUALS(fn.side, Framework::Scripting::Bind::Side::Shared);

        EQUALS(Framework::Scripting::Bind::DescribeConstructor<>(Framework::Scripting::Bind::Doc("Creates.")).params.size(), 0u);
    });

    // --- Side ---

    IT("materialises a Shared member everywhere and a sided member on its own side only", {
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Shared, Framework::Scripting::Bind::Side::Shared), true);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Shared, Framework::Scripting::Bind::Side::Server), true);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Shared, Framework::Scripting::Bind::Side::Client), true);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Server, Framework::Scripting::Bind::Side::Shared), false);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Server, Framework::Scripting::Bind::Side::Server), true);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Server, Framework::Scripting::Bind::Side::Client), false);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Client, Framework::Scripting::Bind::Side::Shared), false);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Client, Framework::Scripting::Bind::Side::Server), false);
        EQUALS(Framework::Scripting::Bind::Materialises(Framework::Scripting::Bind::Side::Client, Framework::Scripting::Bind::Side::Client), true);
    });

    // --- ApiCatalog ---

    IT("finds a recorded description by name", {
        Framework::Scripting::Bind::ClassDesc widget;
        widget.name = "Widget";
        Framework::Scripting::Bind::ClassDesc chat;
        chat.name        = "Chat";
        chat.isNamespace = true;

        Framework::Scripting::Bind::ApiCatalog catalog;
        catalog.Record(&widget);
        catalog.Record(&chat);

        EQUALS(catalog.Find("Widget") == &widget, true);
        EQUALS(catalog.Find("Chat") == &chat, true);
        EQUALS(catalog.Find(std::string_view("Chatter").substr(0, 4)) == &chat, true);
        EQUALS(catalog.Find("Missing") == nullptr, true);
        EQUALS(catalog.Find("") == nullptr, true);
    });

    // The server and the client each have a global Chat, with nothing in common. Which runtimes have a definition
    // is what it declares, not what its members, its constructor or its bases are of.
    IT("finds the description of a name that a runtime of one side has", {
        const auto function = [](const char *name, Framework::Scripting::Bind::Side side) {
            Framework::Scripting::Bind::FunctionDesc desc;
            desc.name = name;
            desc.side = side;
            return desc;
        };
        Framework::Scripting::Bind::ClassDesc serverChat;
        serverChat.name        = "Chat";
        serverChat.isNamespace = true;
        serverChat.side        = Framework::Scripting::Bind::Side::Server;
        serverChat.functions.push_back(function("sendToAll", Framework::Scripting::Bind::Side::Server));
        Framework::Scripting::Bind::ClassDesc clientChat;
        clientChat.name        = "Chat";
        clientChat.isNamespace = true;
        clientChat.side        = Framework::Scripting::Bind::Side::Client;
        clientChat.functions.push_back(function("send", Framework::Scripting::Bind::Side::Client));
        // Shared, though every member of its own is the client's: it has a constructor and a base everywhere.
        Framework::Scripting::Bind::ClassDesc derived;
        derived.name = "Derived";
        derived.bases.push_back("Entity");
        derived.constructor = function("constructor", Framework::Scripting::Bind::Side::Shared);
        derived.functions.push_back(function("focus", Framework::Scripting::Bind::Side::Client));
        derived.properties.push_back({"count", Framework::Scripting::Bind::TypeOf<double>(), "", true, true, Framework::Scripting::Bind::Side::Client});
        // Of the client, though its members are declared for every side.
        Framework::Scripting::Bind::ClassDesc clientOnly;
        clientOnly.name = "Key";
        clientOnly.side = Framework::Scripting::Bind::Side::Client;
        clientOnly.functions.push_back(function("bind", Framework::Scripting::Bind::Side::Shared));
        Framework::Scripting::Bind::ClassDesc empty;
        empty.name = "Marker";

        EQUALS(serverChat.side == Framework::Scripting::Bind::Side::Server && Framework::Scripting::Bind::ClassDesc().side == Framework::Scripting::Bind::Side::Shared, true);
        Framework::Scripting::Bind::ApiCatalog catalog;
        for (const Framework::Scripting::Bind::ClassDesc *desc : {&serverChat, &clientChat, &derived, &clientOnly, &empty}) {
            catalog.Record(desc);
        }

        EQUALS(catalog.Find("Chat") == &serverChat, true);
        EQUALS(catalog.Find("Chat", Framework::Scripting::Bind::Side::Server) == &serverChat, true);
        EQUALS(catalog.Find("Chat", Framework::Scripting::Bind::Side::Client) == &clientChat, true);
        EQUALS(catalog.Find("Derived", Framework::Scripting::Bind::Side::Server) == &derived, true);
        EQUALS(catalog.Find("Derived", Framework::Scripting::Bind::Side::Client) == &derived, true);
        EQUALS(catalog.Find("Marker", Framework::Scripting::Bind::Side::Server) == &empty, true);
        EQUALS(catalog.Find("Key", Framework::Scripting::Bind::Side::Client) == &clientOnly, true);
        EQUALS(catalog.Find("Key", Framework::Scripting::Bind::Side::Server) == nullptr, true);
        EQUALS(catalog.Find("Missing", Framework::Scripting::Bind::Side::Client) == nullptr, true);
        EQUALS(Framework::Scripting::Bind::ExistsIn(serverChat, Framework::Scripting::Bind::Side::Client), false);
        EQUALS(Framework::Scripting::Bind::ExistsIn(serverChat, Framework::Scripting::Bind::Side::Server), true);
        EQUALS(Framework::Scripting::Bind::ExistsIn(clientChat, Framework::Scripting::Bind::Side::Client), true);
        EQUALS(Framework::Scripting::Bind::ExistsIn(derived, Framework::Scripting::Bind::Side::Server), true);
        // What a runtime asks is its own side, but a Shared question has an answer: only what is everywhere.
        EQUALS(Framework::Scripting::Bind::ExistsIn(empty, Framework::Scripting::Bind::Side::Shared), true);
        EQUALS(Framework::Scripting::Bind::ExistsIn(clientChat, Framework::Scripting::Bind::Side::Shared), false);

        // The order of recording decides nothing between the two, for Chat as for Voice.
        Framework::Scripting::Bind::ApiCatalog reversed;
        reversed.Record(&clientChat);
        reversed.Record(&serverChat);
        EQUALS(reversed.Find("Chat", Framework::Scripting::Bind::Side::Server) == &serverChat, true);
        EQUALS(reversed.Find("Chat", Framework::Scripting::Bind::Side::Client) == &clientChat, true);
        Framework::Scripting::Bind::ClassDesc serverVoice;
        serverVoice.name = "Voice";
        serverVoice.side = Framework::Scripting::Bind::Side::Server;
        Framework::Scripting::Bind::ClassDesc clientVoice;
        clientVoice.name = "Voice";
        clientVoice.side = Framework::Scripting::Bind::Side::Client;
        for (const bool serverFirst : {true, false}) {
            Framework::Scripting::Bind::ApiCatalog voices;
            voices.Record(serverFirst ? &serverVoice : &clientVoice);
            voices.Record(serverFirst ? &clientVoice : &serverVoice);
            EQUALS(voices.Find("Voice", Framework::Scripting::Bind::Side::Server) == &serverVoice, true);
            EQUALS(voices.Find("Voice", Framework::Scripting::Bind::Side::Client) == &clientVoice, true);
        }
    });

    IT("keeps descriptions in recording order and ignores one already recorded", {
        Framework::Scripting::Bind::ClassDesc first;
        first.name = "First";
        Framework::Scripting::Bind::ClassDesc second;
        second.name = "Second";

        Framework::Scripting::Bind::ApiCatalog catalog;
        EQUALS(catalog.Entries().empty(), true);
        catalog.Record(&first);
        catalog.Record(&second);
        catalog.Record(&first);

        EQUALS(catalog.Entries().size(), 2u);
        EQUALS(catalog.Entries()[0] == &first, true);
        EQUALS(catalog.Entries()[1] == &second, true);
    });

    IT("reads the name of a description filled in after it was recorded", {
        Framework::Scripting::Bind::ClassDesc late;

        Framework::Scripting::Bind::ApiCatalog catalog;
        catalog.Record(&late);
        late.name = "Late";

        EQUALS(catalog.Find("Late") == &late, true);
    });

    IT("shares one process-wide catalog", { EQUALS(&Framework::Scripting::Bind::ApiCatalog::Get() == &Framework::Scripting::Bind::ApiCatalog::Get(), true); });
});
