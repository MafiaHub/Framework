/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "descriptors.h"

#include <algorithm>
#include <stdexcept>

namespace Framework::Scripting::Bind {
    namespace Type {
        TypeRef Custom(std::string typescript, std::string lua) {
            TypeRef type;
            type.kind       = TypeKind::Custom;
            type.typescript = std::move(typescript);
            type.lua        = std::move(lua);
            return type;
        }

        TypeRef Class(std::string name) {
            TypeRef type;
            type.kind = TypeKind::Class;
            type.name = std::move(name);
            return type;
        }

        TypeRef This() {
            return Detail::Scalar(TypeKind::This);
        }

        TypeRef Any() {
            return Detail::Scalar(TypeKind::Any);
        }
    } // namespace Type

    namespace Detail {
        TypeRef Scalar(TypeKind kind) {
            TypeRef type;
            type.kind = kind;
            return type;
        }

        TypeRef Wrap(TypeKind kind, TypeRef inner) {
            TypeRef type;
            type.kind = kind;
            type.args.push_back(std::move(inner));
            return type;
        }

        FunctionDesc BuildFunctionDesc(std::string name, const Doc &doc, std::vector<ParamDesc> params, TypeRef returns, bool isStatic, Side side) {
            const std::vector<Doc::ParamDoc> &documented = doc.Params();
            if (documented.size() != params.size()) {
                throw std::logic_error("Bind: '" + name + "' takes " + std::to_string(params.size()) + " parameter(s) but its Doc names " + std::to_string(documented.size()));
            }

            for (size_t i = 0; i < params.size(); ++i) {
                ParamDesc &param  = params[i];
                param.name        = documented[i].name;
                param.description = documented[i].description;
                param.optional    = param.optional || documented[i].optional;
                if (documented[i].type) {
                    param.type = *documented[i].type;
                }
            }

            FunctionDesc desc;
            desc.name               = std::move(name);
            desc.description        = doc.Summary();
            desc.params             = std::move(params);
            desc.returns            = doc.ReturnTypeOverride() ? *doc.ReturnTypeOverride() : std::move(returns);
            desc.returnsDescription = doc.ReturnsDescription();
            desc.isStatic           = isStatic;
            desc.side               = side;
            return desc;
        }
    } // namespace Detail

    std::string ToTypeScript(const TypeRef &type) {
        switch (type.kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Integer:
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Optional: return ToTypeScript(type.args.at(0)) + " | undefined";
        case TypeKind::Array: {
            // `number | undefined[]` would be a number or an array of undefined.
            const TypeRef &element = type.args.at(0);
            return element.kind == TypeKind::Optional ? "(" + ToTypeScript(element) + ")[]" : ToTypeScript(element) + "[]";
        }
        case TypeKind::Class: return type.name;
        case TypeKind::This: return "this";
        case TypeKind::Any: return "any";
        case TypeKind::Custom: return type.typescript;
        }
        return {};
    }

    std::string ToLuaAnnotation(const TypeRef &type) {
        switch (type.kind) {
        case TypeKind::Void: return "nil";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Integer: return "integer";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Optional: return ToLuaAnnotation(type.args.at(0)) + "?";
        case TypeKind::Array: {
            const TypeRef &element = type.args.at(0);
            return element.kind == TypeKind::Optional ? "(" + ToLuaAnnotation(element) + ")[]" : ToLuaAnnotation(element) + "[]";
        }
        case TypeKind::Class: return type.name;
        case TypeKind::This: return "self";
        case TypeKind::Any: return "any";
        case TypeKind::Custom: return type.lua;
        }
        return {};
    }

    bool Materialises(Side member, Side runtime) {
        return member == Side::Shared || member == runtime;
    }

    Doc::Doc(std::string summary): _summary(std::move(summary)) {}

    Doc &Doc::Param(std::string name, std::string description) {
        _params.push_back({std::move(name), std::move(description), false, std::nullopt});
        return *this;
    }

    Doc &Doc::OptionalParam(std::string name, std::string description) {
        _params.push_back({std::move(name), std::move(description), true, std::nullopt});
        return *this;
    }

    Doc &Doc::ParamType(TypeRef type) {
        if (_params.empty()) {
            throw std::logic_error("Bind::Doc::ParamType: there is no parameter yet to give a type to");
        }
        _params.back().type = std::move(type);
        return *this;
    }

    Doc &Doc::Returns(std::string description) {
        _returnsDescription = std::move(description);
        return *this;
    }

    Doc &Doc::ReturnType(TypeRef type) {
        _returnType = std::move(type);
        return *this;
    }

    ApiCatalog &ApiCatalog::Get() {
        static ApiCatalog catalog;
        return catalog;
    }

    void ApiCatalog::Record(const ClassDesc *desc) {
        if (std::find(_entries.begin(), _entries.end(), desc) == _entries.end()) {
            _entries.push_back(desc);
        }
    }

    const ClassDesc *ApiCatalog::Find(std::string_view name) const {
        const auto it = std::find_if(_entries.begin(), _entries.end(), [name](const ClassDesc *desc) {
            return desc->name == name;
        });
        return it != _entries.end() ? *it : nullptr;
    }
} // namespace Framework::Scripting::Bind
