/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "native_catalog.h"

#include <utils/hashing.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string_view>

namespace Framework::Scripting::Api {
    namespace {
        std::string Pascal(std::string_view name) {
            std::string result(name);
            if (!result.empty()) {
                result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
            }
            return result;
        }

        std::string ParameterName(const std::string &name) {
            static const std::set<std::string_view> keywords = {"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern",
                "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected", "public",
                "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while"};
            return keywords.contains(name) ? "@" + name : name;
        }

        std::string Xml(std::string_view text) {
            std::string result;
            result.reserve(text.size());
            for (const char c : text) {
                switch (c) {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                default: result += c;
                }
            }
            return result;
        }

        // Reads a NativeValue result `r` as `type`.
        std::string ReadResult(const std::string &type, std::string_view expression) {
            static const std::array<std::pair<std::string_view, std::string_view>, 13> accessors = {{
                {"bool", "AsBool"},
                {"int", "AsInt32"},
                {"uint", "AsUInt32"},
                {"long", "AsInt64"},
                {"ulong", "AsUInt64"},
                {"float", "AsFloat"},
                {"double", "AsDouble"},
                {"string", "AsString"},
                {"Vector2", "AsVector2"},
                {"Vector3", "AsVector3"},
                {"Vector4", "AsVector4"},
                {"Quaternion", "AsQuaternion"},
                {"Color", "AsColor"},
            }};
            for (const auto &[name, accessor] : accessors) {
                if (name == type) {
                    return fmt::format("{}.{}()", expression, accessor);
                }
            }
            return fmt::format("Handles.Wrap<{}>({}.AsHandle())", type, expression);
        }

        // Fills a[index] from `value`; a string needs a pinned buffer that outlives the call.
        void WriteArgument(std::string &out, const std::string &indent, size_t index, const std::string &type, const std::string &value) {
            if (type == "string") {
                out += fmt::format("{}using var s{} = NativeString.Create({}, out a[{}]);\n", indent, index, value, index);
            }
            else {
                out += fmt::format("{}a[{}] = NativeValue.From({});\n", indent, index, value);
            }
        }

        void WriteSummary(std::string &out, const std::string &indent, const std::string &description) {
            if (!description.empty()) {
                out += fmt::format("{}/// <summary>{}</summary>\n", indent, Xml(description));
            }
        }

        void WriteMethod(std::string &out, const NativeMember &member) {
            const std::string indent = "        ";
            const bool isToString    = member.name == "toString" && member.parameters.empty() && member.csharpType == "string";

            WriteSummary(out, indent, member.description);
            for (const auto &parameter : member.parameters) {
                if (!parameter.description.empty()) {
                    out += fmt::format("{}/// <param name=\"{}\">{}</param>\n", indent, parameter.name, Xml(parameter.description));
                }
            }
            if (!member.returnDescription.empty() && member.csharpType != "void") {
                out += fmt::format("{}/// <returns>{}</returns>\n", indent, Xml(member.returnDescription));
            }

            std::string parameters;
            for (const auto &parameter : member.parameters) {
                if (!parameters.empty()) {
                    parameters += ", ";
                }
                parameters += parameter.csharpType + " " + ParameterName(parameter.name);
            }
            const std::string name = isToString ? "ToString" : Pascal(member.name);
            out += fmt::format("{}public {}{} {}({})\n{}{{\n", indent, isToString ? "override " : "", member.csharpType, name, parameters, indent);
            out += indent + "    unsafe\n" + indent + "    {\n";

            const std::string body = indent + "        ";
            const size_t count     = member.parameters.size();
            out += count == 0 ? body + "NativeValue* a = null;\n" : fmt::format("{}NativeValue* a = stackalloc NativeValue[{}];\n", body, count);
            for (size_t i = 0; i < count; ++i) {
                WriteArgument(out, body, i, member.parameters[i].csharpType, ParameterName(member.parameters[i].name));
            }
            const std::string call = fmt::format("Native.Invoke(0x{:08X}u, Id, a, {})", member.id, count);
            if (member.csharpType == "void") {
                out += body + call + ";\n";
            }
            else {
                out += body + "return " + ReadResult(member.csharpType, call) + ";\n";
            }
            out += indent + "    }\n" + indent + "}\n";
        }

        void WriteProperty(std::string &out, const NativeMember &member) {
            const std::string indent = "        ";
            WriteSummary(out, indent, member.description);
            out += fmt::format("{}public {} {}\n{}{{\n", indent, member.csharpType, Pascal(member.name), indent);
            out += fmt::format("{}    get {{ unsafe {{ return {}; }} }}\n", indent, ReadResult(member.csharpType, fmt::format("Native.Invoke(0x{:08X}u, Id, null, 0)", member.id)));
            if (member.setterId != 0) {
                std::string setter;
                WriteArgument(setter, "", 0, member.csharpType, "value");
                if (!setter.empty() && setter.back() == '\n') {
                    setter.pop_back();
                }
                out += fmt::format("{}    set {{ unsafe {{ NativeValue* a = stackalloc NativeValue[1]; {} Native.Invoke(0x{:08X}u, Id, a, 1); }} }}\n", indent, setter, member.setterId);
            }
            out += indent + "}\n";
        }
    } // namespace

    NativeCatalog &NativeCatalog::Get() {
        static NativeCatalog catalog;
        return catalog;
    }

    uint32_t NativeCatalog::IdOf(const std::string &key) {
        return Utils::Hashing::CalculateCRC32(key);
    }

    void NativeCatalog::AddClass(NativeClass *nativeClass) {
        std::scoped_lock lock(_mutex);
        if (std::find(_classes.begin(), _classes.end(), nativeClass) == _classes.end()) {
            _classes.push_back(nativeClass);
        }
    }

    void NativeCatalog::AddThunk(uint32_t id, const std::string &key, NativeThunk thunk) {
        std::scoped_lock lock(_mutex);
        const auto [it, inserted] = _thunks.try_emplace(id, key, thunk);
        if (!inserted && it->second.first != key) {
            throw std::logic_error(fmt::format("Scripting bindings '{}' and '{}' hash to the same native id 0x{:08X}; rename one", it->second.first, key, id));
        }
        it->second.second = thunk;
    }

    NativeThunk NativeCatalog::Find(uint32_t id) const {
        std::scoped_lock lock(_mutex);
        const auto it = _thunks.find(id);
        return it == _thunks.end() ? nullptr : it->second.second;
    }

    std::vector<const NativeClass *> NativeCatalog::GetClasses() const {
        std::scoped_lock lock(_mutex);
        return {_classes.begin(), _classes.end()};
    }

    void NativeCatalog::WriteCSharp(std::ostream &stream, const std::vector<const NativeClass *> &classes) {
        std::string out;
        out += "// <auto-generated>\n";
        out += "//   Generated from the Framework's scripting bindings by Api::NativeCatalog::WriteCSharp.\n";
        out += "//   Do not edit: change the binding and regenerate (docs/scripting_csharp.md).\n";
        out += "// </auto-generated>\n";
        out += "#nullable enable\n";
        out += "using MafiaHub.Framework.Scripting.Interop;\n\n";
        out += "namespace MafiaHub.Framework.Scripting\n{\n";

        for (const NativeClass *nativeClass : classes) {
            WriteSummary(out, "    ", nativeClass->description);
            out += fmt::format("    public partial class {}{}\n    {{\n", nativeClass->name, nativeClass->base.empty() ? "" : " : " + nativeClass->base);
            if (nativeClass->base.empty()) {
                out += "        /// <summary>Network entity identifier this handle refers to.</summary>\n";
                out += "        public ulong Id { get; }\n\n";
                out += fmt::format("        protected internal {}(ulong id)\n        {{\n            Id = id;\n        }}\n", nativeClass->name);
            }
            else {
                out += fmt::format("        protected internal {}(ulong id) : base(id)\n        {{\n        }}\n", nativeClass->name);
            }
            for (const auto &member : nativeClass->members) {
                out += "\n";
                if (member.kind == NativeMember::Kind::Method) {
                    WriteMethod(out, member);
                }
                else {
                    WriteProperty(out, member);
                }
            }
            out += "    }\n\n";
        }

        // Handles arrive from other languages by registered type name; this makes the matching class.
        out += "    internal static partial class Handles\n    {\n";
        out += "        internal static object? Create(string typeName, ulong id)\n        {\n";
        out += "            return typeName switch\n            {\n";
        for (const NativeClass *nativeClass : classes) {
            out += fmt::format("                \"{}\" => new {}(id),\n", nativeClass->name, nativeClass->name);
        }
        out += "                _ => null,\n            };\n        }\n    }\n";
        out += "}\n";

        stream << out;
    }
} // namespace Framework::Scripting::Api
