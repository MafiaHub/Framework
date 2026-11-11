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
    namespace Bind = Framework::Scripting::Bind;

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
        return Bind::ToTypeScript(Bind::TypeOf<T>());
    }

    template <typename T>
    std::string Lua() {
        return Bind::ToLuaAnnotation(Bind::TypeOf<T>());
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
    using namespace Framework::Scripting::Bind;
    using BindTypesTest::Lua;
    using BindTypesTest::Mode;
    using BindTypesTest::TypeScript;
    using BindTypesTest::Widget;

    // --- TypeOf and both spellings, one test per row of the type model ---

    IT("describes bool as Boolean", {
        EQUALS(TypeOf<bool>().kind, TypeKind::Boolean);
        EQUALS(TypeOf<const bool &>().kind, TypeKind::Boolean);
        EQUALS(TypeScript<bool>(), "boolean");
        EQUALS(Lua<bool>(), "boolean");
    });

    IT("describes float and double as Number", {
        EQUALS(TypeOf<float>().kind, TypeKind::Number);
        EQUALS(TypeOf<double>().kind, TypeKind::Number);
        EQUALS(TypeOf<const double &>().kind, TypeKind::Number);
        EQUALS(TypeScript<float>(), "number");
        EQUALS(Lua<float>(), "number");
        EQUALS(TypeScript<double>(), "number");
        EQUALS(Lua<double>(), "number");
    });

    IT("describes integral and enum types as Integer", {
        EQUALS(TypeOf<int>().kind, TypeKind::Integer);
        EQUALS(TypeOf<uint8_t>().kind, TypeKind::Integer);
        EQUALS(TypeOf<int64_t>().kind, TypeKind::Integer);
        EQUALS(TypeOf<uint64_t>().kind, TypeKind::Integer);
        EQUALS(TypeOf<const size_t &>().kind, TypeKind::Integer);
        EQUALS(TypeOf<Mode>().kind, TypeKind::Integer);
        EQUALS(TypeScript<int>(), "number");
        EQUALS(Lua<int>(), "integer");
        EQUALS(TypeScript<uint64_t>(), "number");
        EQUALS(Lua<uint64_t>(), "integer");
        EQUALS(TypeScript<Mode>(), "number");
        EQUALS(Lua<Mode>(), "integer");
    });

    IT("describes std::string, std::string_view and const char * as String", {
        EQUALS(TypeOf<std::string>().kind, TypeKind::String);
        EQUALS(TypeOf<const std::string &>().kind, TypeKind::String);
        EQUALS(TypeOf<std::string_view>().kind, TypeKind::String);
        EQUALS(TypeOf<const char *>().kind, TypeKind::String);
        EQUALS(TypeScript<std::string>(), "string");
        EQUALS(Lua<std::string>(), "string");
        EQUALS(TypeScript<std::string_view>(), "string");
        EQUALS(Lua<std::string_view>(), "string");
        EQUALS(TypeScript<const char *>(), "string");
        EQUALS(Lua<const char *>(), "string");
    });

    IT("describes std::optional<T> as Optional(T)", {
        const TypeRef type = TypeOf<std::optional<int>>();
        EQUALS(type.kind, TypeKind::Optional);
        EQUALS(type.args.size(), 1u);
        EQUALS(type.args[0].kind, TypeKind::Integer);
        EQUALS(TypeOf<const std::optional<int> &>().kind, TypeKind::Optional);
        EQUALS(TypeScript<std::optional<int>>(), "number | undefined");
        EQUALS(Lua<std::optional<int>>(), "integer?");
        EQUALS(TypeScript<std::optional<std::string>>(), "string | undefined");
        EQUALS(Lua<std::optional<std::string>>(), "string?");
    });

    IT("describes std::vector<T> as Array(T)", {
        const TypeRef type = TypeOf<std::vector<std::string>>();
        EQUALS(type.kind, TypeKind::Array);
        EQUALS(type.args.size(), 1u);
        EQUALS(type.args[0].kind, TypeKind::String);
        EQUALS(TypeOf<const std::vector<float> &>().kind, TypeKind::Array);
        EQUALS(TypeScript<std::vector<std::string>>(), "string[]");
        EQUALS(Lua<std::vector<std::string>>(), "string[]");
        EQUALS(TypeScript<std::vector<std::vector<float>>>(), "number[][]");
        EQUALS(Lua<std::vector<std::vector<float>>>(), "number[][]");
        EQUALS(TypeScript<std::optional<std::vector<Widget>>>(), "Widget[] | undefined");
        EQUALS(Lua<std::optional<std::vector<Widget>>>(), "Widget[]?");
    });

    IT("keeps an optional element apart from the array brackets", {
        EQUALS(TypeScript<std::vector<std::optional<int>>>(), "(number | undefined)[]");
        EQUALS(Lua<std::vector<std::optional<int>>>(), "(integer?)[]");
    });

    IT("describes a bound class by its script name, through references and pointers", {
        EQUALS(TypeOf<Widget>().kind, TypeKind::Class);
        EQUALS(TypeOf<Widget>().name, "Widget");
        EQUALS(TypeOf<Widget &>().name, "Widget");
        EQUALS(TypeOf<const Widget &>().name, "Widget");
        EQUALS(TypeOf<Widget *>().name, "Widget");
        EQUALS(TypeOf<const Widget *>().kind, TypeKind::Class);
        EQUALS(TypeScript<const Widget &>(), "Widget");
        EQUALS(Lua<const Widget &>(), "Widget");
        EQUALS(TypeScript<Widget *>(), "Widget");
        EQUALS(Lua<Widget *>(), "Widget");
    });

    IT("describes glm vectors and quaternions as the value classes", {
        EQUALS(TypeOf<glm::vec2>().kind, TypeKind::Class);
        EQUALS(TypeOf<glm::vec2>().name, "Vector2");
        EQUALS(TypeOf<glm::vec3>().name, "Vector3");
        EQUALS(TypeOf<const glm::vec3 &>().name, "Vector3");
        EQUALS(TypeOf<glm::vec4>().name, "Vector4");
        EQUALS(TypeOf<glm::quat>().name, "Quaternion");
        EQUALS(TypeScript<glm::vec2>(), "Vector2");
        EQUALS(Lua<glm::vec2>(), "Vector2");
        EQUALS(TypeScript<glm::vec3>(), "Vector3");
        EQUALS(Lua<glm::vec3>(), "Vector3");
        EQUALS(TypeScript<glm::vec4>(), "Vector4");
        EQUALS(Lua<glm::vec4>(), "Vector4");
        EQUALS(TypeScript<glm::quat>(), "Quaternion");
        EQUALS(Lua<glm::quat>(), "Quaternion");
    });

    IT("describes a method returning a reference to its own class as This", {
        const FunctionDesc grow = DescribeFunction<decltype(&Widget::grow)>("grow", Doc("Grows.").Param("amount", "Size to add."), false, Side::Shared);
        EQUALS(grow.returns.kind, TypeKind::This);
        EQUALS(ToTypeScript(grow.returns), "this");
        EQUALS(ToLuaAnnotation(grow.returns), "self");

        const FunctionDesc reset = DescribeFunction<decltype(&Widget::reset)>("reset", Doc("Resets."), false, Side::Shared);
        EQUALS(reset.returns.kind, TypeKind::This);

        EQUALS(Type::This().kind, TypeKind::This);
        EQUALS(ToTypeScript(Type::This()), "this");
        EQUALS(ToLuaAnnotation(Type::This()), "self");
    });

    IT("describes void as Void", {
        EQUALS(TypeOf<void>().kind, TypeKind::Void);
        EQUALS(TypeRef {}.kind, TypeKind::Void);
        EQUALS(TypeScript<void>(), "void");
        EQUALS(Lua<void>(), "nil");
    });

    IT("spells Any, a class and a custom type as given", {
        EQUALS(Type::Any().kind, TypeKind::Any);
        EQUALS(ToTypeScript(Type::Any()), "any");
        EQUALS(ToLuaAnnotation(Type::Any()), "any");

        EQUALS(Type::Class("Player").kind, TypeKind::Class);
        EQUALS(ToTypeScript(Type::Class("Player")), "Player");
        EQUALS(ToLuaAnnotation(Type::Class("Player")), "Player");

        const TypeRef custom = Type::Custom("{ x: number; y: number }", "{ x: number, y: number }");
        EQUALS(custom.kind, TypeKind::Custom);
        EQUALS(ToTypeScript(custom), "{ x: number; y: number }");
        EQUALS(ToLuaAnnotation(custom), "{ x: number, y: number }");
    });

    // --- DescribeFunction ---

    IT("takes parameter types from the signature and names from the Doc, in order", {
        const Doc doc         = Doc("Scales the widget.").Param("factor", "Multiplier.").Param("steps", "Times to apply it.").Returns("The scaled size.");
        const FunctionDesc fn = DescribeFunction<decltype(&Widget::scaled)>("scaled", doc, false, Side::Server);

        EQUALS(fn.name, "scaled");
        EQUALS(fn.description, "Scales the widget.");
        EQUALS(fn.returnsDescription, "The scaled size.");
        EQUALS(fn.returns.kind, TypeKind::Number);
        EQUALS(fn.isStatic, false);
        EQUALS(fn.side, Side::Server);

        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[0].name, "factor");
        EQUALS(fn.params[0].description, "Multiplier.");
        EQUALS(fn.params[0].type.kind, TypeKind::Number);
        EQUALS(fn.params[0].optional, false);
        EQUALS(fn.params[1].name, "steps");
        EQUALS(fn.params[1].description, "Times to apply it.");
    });

    IT("flags a std::optional parameter optional and unwraps its type", {
        const Doc doc         = Doc("Scales the widget.").Param("factor", "Multiplier.").Param("steps", "Times to apply it.");
        const FunctionDesc fn = DescribeFunction<decltype(&Widget::scaled)>("scaled", doc, false, Side::Shared);

        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[1].optional, true);
        EQUALS(fn.params[1].type.kind, TypeKind::Integer);
        EQUALS(ToTypeScript(fn.params[1].type), "number");
        EQUALS(ToLuaAnnotation(fn.params[1].type), "integer");
    });

    IT("excludes the receiver of a member function, const and noexcept included", {
        EQUALS(DescribeFunction<decltype(&Widget::clone)>("clone", Doc("Copies."), false, Side::Shared).params.size(), 0u);
        EQUALS(DescribeFunction<decltype(&Widget::toString)>("toString", Doc("Formats."), false, Side::Shared).params.size(), 0u);
        EQUALS(DescribeFunction<decltype(&Widget::clear)>("clear", Doc("Clears."), false, Side::Shared).returns.kind, TypeKind::Void);
        EQUALS(ToTypeScript(DescribeFunction<decltype(&Widget::clone)>("clone", Doc("Copies."), false, Side::Shared).returns), "Widget");
        EQUALS(ToTypeScript(DescribeFunction<decltype(&Widget::toString)>("toString", Doc("Formats."), false, Side::Shared).returns), "string");
    });

    IT("returns This only for a non-const reference to the method's own class", {
        EQUALS(DescribeFunction<decltype(&Widget::view)>("view", Doc("Views."), false, Side::Shared).returns.kind, TypeKind::Class);
        EQUALS(DescribeFunction<decltype(&Widget::clone)>("clone", Doc("Copies."), false, Side::Shared).returns.kind, TypeKind::Class);

        const FunctionDesc same = DescribeFunction<decltype(&BindTypesTest::Same)>("same", Doc("Returns it.").Param("widget", "Any widget."), true, Side::Shared);
        EQUALS(same.returns.kind, TypeKind::Class);
        EQUALS(same.returns.name, "Widget");
    });

    IT("takes every parameter of a free function", {
        const Doc doc         = Doc("Measures.").Param("a", "First point.").Param("b", "Second point.");
        const FunctionDesc fn = DescribeFunction<decltype(&BindTypesTest::Distance)>("distance", doc, true, Side::Client);

        EQUALS(fn.isStatic, true);
        EQUALS(fn.side, Side::Client);
        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[0].type.name, "Vector3");
        EQUALS(fn.params[1].name, "b");
        EQUALS(fn.returns.kind, TypeKind::Number);

        EQUALS(DescribeFunction<decltype(BindTypesTest::Distance)>("distance", doc, true, Side::Shared).params.size(), 2u);
        EQUALS(DescribeFunction<decltype(&Widget::make)>("make", Doc("Makes.").Param("size", "Initial size."), true, Side::Shared).returns.name, "Widget");
    });

    IT("takes every parameter of a lambda", {
        const auto lambda     = [](const Widget &, int count, const std::string &) { return count > 0; };
        const Doc doc         = Doc("Checks.").Param("widget", "Widget.").Param("count", "Count.").Param("label", "Label.");
        const FunctionDesc fn = DescribeFunction<decltype(lambda)>("check", doc, true, Side::Shared);

        EQUALS(fn.params.size(), 3u);
        EQUALS(fn.params[0].type.name, "Widget");
        EQUALS(fn.params[1].type.kind, TypeKind::Integer);
        EQUALS(fn.params[2].type.kind, TypeKind::String);
        EQUALS(fn.returns.kind, TypeKind::Boolean);

        EQUALS(DescribeFunction<decltype(+lambda)>("check", doc, true, Side::Shared).params.size(), 3u);
    });

    IT("throws a logic_error naming the function when the Doc names another number of parameters", {
        const std::string tooFew = BindTypesTest::LogicErrorOf([] {
            DescribeFunction<decltype(&Widget::scaled)>("scaled", Doc("Scales.").Param("factor", "Multiplier."), false, Side::Shared);
        });
        EQUALS(tooFew.find("'scaled'") != std::string::npos, true);

        const std::string tooMany = BindTypesTest::LogicErrorOf([] {
            DescribeFunction<decltype(&Widget::clone)>("clone", Doc("Copies.").Param("extra", "Not a parameter."), false, Side::Shared);
        });
        EQUALS(tooMany.find("'clone'") != std::string::npos, true);

        const std::string constructor = BindTypesTest::LogicErrorOf([] {
            DescribeConstructor<float, float>(Doc("Creates.").Param("x", "X."));
        });
        EQUALS(constructor.find("'constructor'") != std::string::npos, true);
    });

    IT("lets ParamType override the type of the last parameter", {
        const auto lambda     = [](int, const Widget &) {};
        const Doc doc         = Doc("Moves.").Param("id", "Identifier.").Param("target", "Destination.").ParamType(Type::Custom("{ x: number }", "{ x: number }"));
        const FunctionDesc fn = DescribeFunction<decltype(lambda)>("move", doc, true, Side::Shared);

        EQUALS(fn.params.size(), 2u);
        EQUALS(fn.params[0].type.kind, TypeKind::Integer);
        EQUALS(fn.params[1].type.kind, TypeKind::Custom);
        EQUALS(ToTypeScript(fn.params[1].type), "{ x: number }");
        EQUALS(ToLuaAnnotation(fn.params[1].type), "{ x: number }");
        EQUALS(fn.params[1].name, "target");
    });

    IT("refuses ParamType before any parameter", {
        const std::string message = BindTypesTest::LogicErrorOf([] {
            Doc("Moves.").ParamType(Type::Any());
        });
        EQUALS(message.empty(), false);
    });

    IT("lets OptionalParam document a parameter optional whatever its C++ type", {
        const Doc doc         = Doc("Grows.").OptionalParam("amount", "Size to add; one when omitted.");
        const FunctionDesc fn = DescribeFunction<decltype(&Widget::grow)>("grow", doc, false, Side::Shared);

        EQUALS(fn.params.size(), 1u);
        EQUALS(fn.params[0].optional, true);
        EQUALS(fn.params[0].type.kind, TypeKind::Number);
        EQUALS(fn.params[0].name, "amount");
        EQUALS(fn.params[0].description, "Size to add; one when omitted.");
    });

    IT("lets ReturnType override the derived return type", {
        const Doc doc         = Doc("Formats.").Returns("Plain object.").ReturnType(Type::Custom("{ size: number }", "{ size: number }"));
        const FunctionDesc fn = DescribeFunction<decltype(&Widget::toString)>("toJSON", doc, false, Side::Shared);

        EQUALS(fn.returns.kind, TypeKind::Custom);
        EQUALS(ToTypeScript(fn.returns), "{ size: number }");
        EQUALS(fn.returnsDescription, "Plain object.");
    });

    IT("describes a constructor from its argument types", {
        const Doc doc         = Doc("Creates a vector.").Param("x", "Initial X.").Param("y", "Initial Y.").Param("z", "Initial Z.");
        const FunctionDesc fn = DescribeConstructor<float, float, float>(doc);

        EQUALS(fn.name, "constructor");
        EQUALS(fn.description, "Creates a vector.");
        EQUALS(fn.params.size(), 3u);
        EQUALS(fn.params[2].name, "z");
        EQUALS(ToTypeScript(fn.params[2].type), "number");
        EQUALS(ToTypeScript(fn.returns), "void");
        EQUALS(fn.isStatic, false);
        EQUALS(fn.side, Side::Shared);

        EQUALS(DescribeConstructor<>(Doc("Creates.")).params.size(), 0u);
    });

    // --- Side ---

    IT("materialises a Shared member everywhere and a sided member on its own side only", {
        EQUALS(Materialises(Side::Shared, Side::Shared), true);
        EQUALS(Materialises(Side::Shared, Side::Server), true);
        EQUALS(Materialises(Side::Shared, Side::Client), true);
        EQUALS(Materialises(Side::Server, Side::Shared), false);
        EQUALS(Materialises(Side::Server, Side::Server), true);
        EQUALS(Materialises(Side::Server, Side::Client), false);
        EQUALS(Materialises(Side::Client, Side::Shared), false);
        EQUALS(Materialises(Side::Client, Side::Server), false);
        EQUALS(Materialises(Side::Client, Side::Client), true);
    });

    // --- ApiCatalog ---

    IT("finds a recorded description by name", {
        ClassDesc widget;
        widget.name = "Widget";
        ClassDesc chat;
        chat.name        = "Chat";
        chat.isNamespace = true;

        ApiCatalog catalog;
        catalog.Record(&widget);
        catalog.Record(&chat);

        EQUALS(catalog.Find("Widget") == &widget, true);
        EQUALS(catalog.Find("Chat") == &chat, true);
        EQUALS(catalog.Find(std::string_view("Chatter").substr(0, 4)) == &chat, true);
        EQUALS(catalog.Find("Missing") == nullptr, true);
        EQUALS(catalog.Find("") == nullptr, true);
    });

    IT("keeps descriptions in recording order and ignores one already recorded", {
        ClassDesc first;
        first.name = "First";
        ClassDesc second;
        second.name = "Second";

        ApiCatalog catalog;
        EQUALS(catalog.Entries().empty(), true);
        catalog.Record(&first);
        catalog.Record(&second);
        catalog.Record(&first);

        EQUALS(catalog.Entries().size(), 2u);
        EQUALS(catalog.Entries()[0] == &first, true);
        EQUALS(catalog.Entries()[1] == &second, true);
    });

    IT("reads the name of a description filled in after it was recorded", {
        ClassDesc late;

        ApiCatalog catalog;
        catalog.Record(&late);
        late.name = "Late";

        EQUALS(catalog.Find("Late") == &late, true);
    });

    IT("shares one process-wide catalog", {
        EQUALS(&ApiCatalog::Get() == &ApiCatalog::Get(), true);
    });
});
