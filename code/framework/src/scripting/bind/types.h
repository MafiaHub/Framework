/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Framework::Scripting::Bind {
    enum class TypeKind : uint8_t {
        Void,
        Boolean,
        Integer,
        Number,
        String,
        Optional,
        Array,
        Class,
        This,
        Any,
        Custom
    };

    // A script-facing type, independent of the language it is spelled in.
    struct TypeRef {
        TypeKind kind = TypeKind::Void;
        std::string name;          // Class: the script name of the class
        std::vector<TypeRef> args; // Optional, Array: the one wrapped type
        std::string typescript;    // Custom: the TypeScript spelling
        std::string lua;           // Custom: the Lua annotation spelling
    };

    namespace Type {
        // A type spelled by hand, for what the model cannot derive (an option object, a union).
        TypeRef Custom(std::string typescript, std::string lua);
        TypeRef Class(std::string name);
        TypeRef This();
        TypeRef Any();
    } // namespace Type

    // The script name of a bound C++ class. Specialised by FW_SCRIPT_CLASS, never by hand.
    template <typename T>
    struct ScriptClassName;

    namespace Detail {
        template <typename T>
        concept HasScriptClassName = requires { ScriptClassName<T>::value; };

        template <typename T>
        struct IsOptional final: std::false_type {};
        template <typename T>
        struct IsOptional<std::optional<T>> final: std::true_type {};

        template <typename T>
        struct IsVector final: std::false_type {};
        template <typename T, typename Allocator>
        struct IsVector<std::vector<T, Allocator>> final: std::true_type {};

        TypeRef Scalar(TypeKind kind);
        TypeRef Wrap(TypeKind kind, TypeRef inner);

        // What a runtime needs of a bound class, without its C++ type (lua_class.h).
        struct LuaClass;

        // The definition of the C++ type T, or nullptr until Class<T> is constructed.
        template <typename T>
        struct LuaClassOf final {
            static inline const LuaClass *definition = nullptr;
        };
    } // namespace Detail

    // The script type of a C++ type, per the type model (spec 1.2). References, cv-qualifiers and a pointer to a
    // class are looked through. A type outside the model does not compile.
    template <typename T>
    TypeRef TypeOf() {
        using Value = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Value, const char *>) {
            return Detail::Scalar(TypeKind::String);
        }
        else if constexpr (std::is_pointer_v<Value>) {
            using Pointee = std::remove_cv_t<std::remove_pointer_t<Value>>;
            static_assert(std::is_class_v<Pointee>, "Bind::TypeOf: the only pointers a script API can take or return are const char * and a pointer to a bound class");
            return TypeOf<Pointee>();
        }
        else if constexpr (std::is_void_v<Value>) {
            return Detail::Scalar(TypeKind::Void);
        }
        else if constexpr (std::is_same_v<Value, bool>) {
            return Detail::Scalar(TypeKind::Boolean);
        }
        else if constexpr (std::is_integral_v<Value> || std::is_enum_v<Value>) {
            return Detail::Scalar(TypeKind::Integer);
        }
        else if constexpr (std::is_floating_point_v<Value>) {
            return Detail::Scalar(TypeKind::Number);
        }
        else if constexpr (std::is_same_v<Value, std::string> || std::is_same_v<Value, std::string_view>) {
            return Detail::Scalar(TypeKind::String);
        }
        else if constexpr (Detail::IsOptional<Value>::value) {
            return Detail::Wrap(TypeKind::Optional, TypeOf<typename Value::value_type>());
        }
        else if constexpr (Detail::IsVector<Value>::value) {
            return Detail::Wrap(TypeKind::Array, TypeOf<typename Value::value_type>());
        }
        else if constexpr (std::is_same_v<Value, glm::vec2>) {
            return Type::Class("Vector2");
        }
        else if constexpr (std::is_same_v<Value, glm::vec3>) {
            return Type::Class("Vector3");
        }
        else if constexpr (std::is_same_v<Value, glm::vec4>) {
            return Type::Class("Vector4");
        }
        else if constexpr (std::is_same_v<Value, glm::quat>) {
            return Type::Class("Quaternion");
        }
        else if constexpr (Detail::HasScriptClassName<Value>) {
            return Type::Class(ScriptClassName<Value>::value);
        }
        else {
            // Exactly one of the two fails, so the error names the actual problem.
            static_assert(!std::is_class_v<Value>, "Bind::TypeOf: this class has no script name; add FW_SCRIPT_CLASS(Type, \"Name\") at global namespace scope after its definition");
            static_assert(std::is_class_v<Value>, "Bind::TypeOf: this type is not part of the script type model (bool, integers, enums, float, double, strings, std::optional, std::vector, glm vectors, bound classes)");
            return {};
        }
    }

    std::string ToTypeScript(const TypeRef &type);
    std::string ToLuaAnnotation(const TypeRef &type);
} // namespace Framework::Scripting::Bind

// Gives the bound class `CppType` the script name `Name`, so TypeOf can describe it wherever a signature uses it.
// Use at global namespace scope, after the class definition and before the first signature that names the class
// is described.
#define FW_SCRIPT_CLASS(CppType, Name)                                                                                                                                                                                                                                                 \
    template <>                                                                                                                                                                                                                                                                        \
    struct Framework::Scripting::Bind::ScriptClassName<CppType> final {                                                                                                                                                                                                                \
        static constexpr const char *value = Name;                                                                                                                                                                                                                                     \
    }
