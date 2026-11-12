/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_definitions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace Framework::Scripting::Bind {
    namespace {
        // What a script finds changed in its state (scripting/lua/lua_sandbox.cpp), and what the catalog does not
        // hold. The side goes between the two.
        constexpr std::string_view kHeaderStart = "---@meta\n"
                                                  "\n"
                                                  "-- MafiaHub Framework: the Lua API of a ";
        constexpr std::string_view kHeaderEnd = " resource, for the Lua language server (LuaLS).\n"
                                                "-- Generated from the definitions of the builtins. Do not edit.\n"
                                                "--\n"
                                                "-- A resource runs in a sandbox. Of the standard library it has base, coroutine, table, string, utf8 and\n"
                                                "-- math, and of os only time, clock, date and difftime. There is no io, no debug and no package.\n"
                                                "--   removed   dofile, loadfile, string.dump\n"
                                                "--   changed   load            takes a string chunk only, always compiled as text\n"
                                                "--             collectgarbage  answers \"count\" only\n"
                                                "--             print           writes a line to the log of the resource\n"
                                                "--             require         reads the files of the resource and nothing else\n"
                                                "--             setmetatable    refuses a metatable with __gc\n"
                                                "--             coroutine.resume, coroutine.close\n"
                                                "--                             refuse the coroutine the host runs the script in\n"
                                                "--\n"
                                                "-- A field marked [read-only] cannot be assigned. A function marked ---@async waits: the coroutine that\n"
                                                "-- calls it is suspended until the result is known, then the call returns it, or raises. Every chunk,\n"
                                                "-- handler and timer callback runs in a coroutine.\n"
                                                "--\n"
                                                "-- What this file cannot say:\n"
                                                "--   - the events the framework raises and what their handlers receive (Events.on);\n"
                                                "--   - the fields of an option table where a parameter is typed `table`;\n"
                                                "--   - the comparisons of a class (==, <, <=) and an operator whose left operand is not the class, which\n"
                                                "--     have no annotation: the description of the class names them;\n"
                                                "--   - what a class adds for Lua outside its declared members, and what a game adds to all of this.\n";

        constexpr std::array<std::string_view, 22> kKeywords = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

        // The operators LuaLS has an annotation for, of those a class can declare, and how each is written.
        struct Operator final {
            std::string_view event;
            std::string_view symbol;
            bool annotated;
        };
        constexpr std::array<Operator, 10> kOperators = {{{"add", "+", true}, {"sub", "-", true}, {"mul", "*", true}, {"div", "/", true}, {"unm", "-", true}, {"eq", "==", false}, {"lt", "<", false}, {"le", "<=", false}, {"len", "#", true}, {"concat", "..", true}}};

        const Operator *FindOperator(std::string_view event) {
            const auto it = std::find_if(kOperators.begin(), kOperators.end(), [event](const Operator &candidate) {
                return candidate.event == event;
            });
            return it != kOperators.end() ? &*it : nullptr;
        }

        bool IsWordCharacter(char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_';
        }

        // A description as a Lua author reads it. The definitions word theirs for JavaScript: where `undefined` or
        // `null` stands as a word of its own, Lua has nil.
        std::string ForLua(std::string_view text) {
            std::string out(text);
            for (const std::string_view word : {std::string_view("undefined"), std::string_view("null")}) {
                for (size_t at = out.find(word); at != std::string::npos; at = out.find(word, at + 1)) {
                    const bool starts = at == 0 || !IsWordCharacter(out[at - 1]);
                    const bool ends   = at + word.size() == out.size() || !IsWordCharacter(out[at + word.size()]);
                    if (starts && ends) {
                        out.replace(at, word.size(), "nil");
                    }
                }
            }
            return out;
        }

        // The description of a read-only property behind the marker that says so: without a "Read-only" of its own
        // in front, which would say it twice.
        std::string AfterReadOnlyMarker(std::string description) {
            for (const std::string_view prefix : {std::string_view("Read-only. "), std::string_view("Read-only ")}) {
                if (description.starts_with(prefix) && description.size() > prefix.size()) {
                    description.erase(0, prefix.size());
                    description[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(description[0])));
                    break;
                }
            }
            return description;
        }

        // On one line, for the description an annotation ends with.
        std::string Inline(std::string_view text) {
            std::string line = ForLua(text);
            std::replace(line.begin(), line.end(), '\n', ' ');
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            return line;
        }

        // `text` as the description above a definition: every line of it behind `---`.
        void Describe(std::string &out, std::string_view described) {
            const std::string forLua = ForLua(described);
            std::string_view text    = forLua;
            while (!text.empty()) {
                const size_t end      = text.find('\n');
                std::string_view line = text.substr(0, end);
                if (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }
                out += "---";
                out += line;
                out += '\n';
                text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
            }
        }

        std::string ParamName(const ParamDesc &param) {
            if (param.rest) {
                return "...";
            }
            return std::find(kKeywords.begin(), kKeywords.end(), param.name) != kKeywords.end() ? param.name + "_" : param.name;
        }

        // The type of a parameter a script may omit is spelled without the `?` the name already carries.
        const TypeRef &ParamType(const ParamDesc &param) {
            return param.optional && param.type.kind == TypeKind::Optional ? param.type.args.at(0) : param.type;
        }

        bool IsAsync(const FunctionDesc &function) {
            return function.returns.kind == TypeKind::Custom && function.returns.typescript.starts_with("Promise<");
        }

        // The annotation of what a function returns, or "" when it returns nothing.
        std::string ReturnType(const FunctionDesc &function) {
            if (function.returns.kind == TypeKind::Void) {
                return {};
            }
            std::string type = ToLuaAnnotation(function.returns);
            return type == "nil" ? std::string() : type;
        }

        std::string ParamList(const FunctionDesc &function) {
            std::string list;
            for (const ParamDesc &param : function.params) {
                list += (list.empty() ? "" : ", ") + ParamName(param);
            }
            return list;
        }

        void WriteParams(std::string &out, const FunctionDesc &function) {
            for (const ParamDesc &param : function.params) {
                out += "---@param " + ParamName(param) + (param.optional && !param.rest ? "? " : " ") + ToLuaAnnotation(ParamType(param));
                if (!param.description.empty()) {
                    out += " " + Inline(param.description);
                }
                out += '\n';
            }
        }

        // `function <owner><separator><name>(...) end` under its description and annotations. `returns` replaces
        // what the function is described as returning, for a constructor.
        void WriteFunction(std::string &out, const FunctionDesc &function, const std::string &qualifiedName, const std::string &returns = {}) {
            out += '\n';
            Describe(out, function.description);
            if (IsAsync(function)) {
                out += "---@async\n";
            }
            WriteParams(out, function);
            if (const std::string type = returns.empty() ? ReturnType(function) : returns; !type.empty()) {
                out += "---@return " + type;
                if (!function.returnsDescription.empty()) {
                    out += " # " + Inline(function.returnsDescription);
                }
                out += '\n';
            }
            out += "function " + qualifiedName + "(" + ParamList(function) + ") end\n";
        }

        // `fun(a: A, b?: B): Name`, the signature a class is called with.
        std::string CallSignature(const FunctionDesc &constructor, const std::string &className) {
            std::string params;
            for (const ParamDesc &param : constructor.params) {
                params += (params.empty() ? "" : ", ") + ParamName(param) + (param.optional && !param.rest ? "?: " : ": ") + ToLuaAnnotation(ParamType(param));
            }
            return "fun(" + params + "): " + className;
        }

        bool IsClass(const TypeRef &type, const std::string &name) {
            return type.kind == TypeKind::Class && type.name == name;
        }

        // The operators of a class: an `---@operator` line for each one LuaLS can be told of, in `annotations`, and
        // the others as a script writes them, in `unannotated`.
        void DescribeOperators(const ClassDesc &desc, std::string &annotations, std::vector<std::string> &unannotated) {
            for (const LuaOperatorDesc &declared : desc.luaOperators) {
                const Operator *known = FindOperator(declared.event);
                if (known == nullptr || declared.operands.empty()) {
                    continue;
                }
                const bool unary = declared.operands.size() == 1;
                if (known->annotated && IsClass(declared.operands[0], desc.name)) {
                    annotations += "---@operator " + declared.event + (unary ? "" : "(" + ToLuaAnnotation(declared.operands[1]) + ")") + ": " + ToLuaAnnotation(declared.returns) + "\n";
                    continue;
                }
                std::string written = unary ? std::string(known->symbol) + ToLuaAnnotation(declared.operands[0]) : ToLuaAnnotation(declared.operands[0]) + " " + std::string(known->symbol) + " " + ToLuaAnnotation(declared.operands[1]);
                unannotated.push_back("`" + written + "`");
            }
        }

        // " [read-only] <description>", " <description>" or "": what follows the type of a property. The marker
        // is fixed, so that a reader of the file can tell a property a script cannot assign.
        std::string PropertyDescription(const PropertyDesc &property) {
            std::string description = Inline(property.description);
            if (property.readonly) {
                description = AfterReadOnlyMarker(std::move(description));
                return std::string(" ") + std::string(kReadOnlyMarker) + (description.empty() ? "" : " " + description);
            }
            return description.empty() ? std::string() : " " + description;
        }

        void WriteDefinition(std::string &out, const ClassDesc &desc, Side side) {
            std::string operators;
            std::vector<std::string> unannotated;
            DescribeOperators(desc, operators, unannotated);

            out += '\n';
            Describe(out, desc.description);
            if (!unannotated.empty()) {
                out += desc.description.empty() ? "" : "---\n";
                out += "---Also has the operators";
                for (size_t i = 0; i < unannotated.size(); ++i) {
                    out += (i == 0 ? " " : ", ") + unannotated[i];
                }
                out += ".\n";
            }
            if (!desc.isNamespace && !desc.global) {
                out += desc.description.empty() && unannotated.empty() ? "" : "---\n";
                out += "---Not a global: a script is given the objects of this class and cannot create one.\n";
            }

            out += "---@class " + desc.name;
            for (size_t i = 0; i < desc.bases.size(); ++i) {
                out += (i == 0 ? ": " : ", ") + desc.bases[i];
            }
            out += '\n';

            // A static property of a class is a field of its table alone, written below it.
            const auto isField = [&desc, side](const PropertyDesc &property) {
                return Materialises(property.side, side) && (desc.isNamespace || !property.isStatic);
            };
            for (const PropertyDesc &property : desc.properties) {
                if (isField(property)) {
                    out += "---@field " + property.name + " " + ToLuaAnnotation(property.type) + PropertyDescription(property) + "\n";
                }
            }
            out += operators;
            if (desc.constructor) {
                out += "---@overload " + CallSignature(*desc.constructor, desc.name) + "\n";
            }
            out += (desc.isNamespace || desc.global ? "" : "local ") + desc.name + " = {}\n";

            for (const PropertyDesc &property : desc.properties) {
                if (Materialises(property.side, side) && !isField(property)) {
                    out += '\n';
                    Describe(out, PropertyDescription(property).erase(0, 1));
                    out += "---@type " + ToLuaAnnotation(property.type) + "\n" + desc.name + "." + property.name + " = nil\n";
                }
            }
            if (desc.constructor) {
                WriteFunction(out, *desc.constructor, desc.name + ".new", desc.name);
            }
            for (const FunctionDesc &function : desc.functions) {
                if (Materialises(function.side, side)) {
                    WriteFunction(out, function, desc.name + (desc.isNamespace || function.isStatic ? "." : ":") + function.name);
                }
            }
        }

        FunctionDesc Global(std::string name, std::string description, std::vector<ParamDesc> params, std::string returns = {}, std::string returnsDescription = {}, bool waits = false) {
            FunctionDesc function;
            function.name        = std::move(name);
            function.description = std::move(description);
            function.params      = std::move(params);
            if (waits) {
                // As a function of a builtin that waits is described: by the promise JavaScript would be given.
                function.returns = Type::Custom("Promise<void>", "nil");
            }
            else if (!returns.empty()) {
                function.returns = Type::Custom("unknown", std::move(returns));
            }
            function.returnsDescription = std::move(returnsDescription);
            function.isStatic           = true;
            return function;
        }

        ParamDesc Param(std::string name, std::string lua, std::string description, bool optional = false, bool rest = false) {
            ParamDesc param;
            param.name        = std::move(name);
            param.type        = Type::Custom("unknown", std::move(lua));
            param.description = std::move(description);
            param.optional    = optional;
            param.rest        = rest;
            return param;
        }
    } // namespace

    const std::vector<FunctionDesc> &LuaRuntimeGlobals() {
        static const std::vector<FunctionDesc> globals = {
            Global("setTimeout", "Calls a function once after a delay, in a coroutine of its own.",
                {Param("callback", "fun(...: any)", "Function to call."), Param("milliseconds", "number", "Delay before the call; none when it is missing or below 1.", true), Param("args", "any", "Arguments passed to the callback.", false, true)}, "integer",
                "Id to pass to clearTimeout."),
            Global("setInterval", "Calls a function repeatedly, waiting the delay between calls, each time in a coroutine of its own.",
                {Param("callback", "fun(...: any)", "Function to call."), Param("milliseconds", "number", "Delay between calls; 1 when it is missing or below 1.", true), Param("args", "any", "Arguments passed to the callback.", false, true)}, "integer",
                "Id to pass to clearInterval."),
            Global("clearTimeout", "Cancels a pending setTimeout.", {Param("id", "integer", "Id returned when the timer was set; anything else is ignored.", true)}),
            Global("clearInterval", "Cancels a setInterval.", {Param("id", "integer", "Id returned when the timer was set; anything else is ignored.", true)}),
            Global("sleep", "Suspends the calling coroutine for a time. Everything else goes on running meanwhile.", {Param("milliseconds", "number", "How long to wait; until the next tick when it is missing or below 1.", true)}, {}, {}, /*waits*/ true),
            Global("spawn", "Runs a function in a coroutine of its own, at once and as far as its first wait. What it raises is not raised in the caller.",
                {Param("callback", "fun(...: any)", "Function to run."), Param("args", "any", "Arguments passed to the function.", false, true)}),
            Global("print", "Writes its arguments to the log of the resource, on one line, each as tostring spells it.", {Param("values", "any", "Values to write.", false, true)}),
            Global("require", "Loads a Lua file of the resource once and returns what it returned, true when it returned nothing. Nothing outside the resource can be loaded.",
                {Param("name", "string", "Name of the module: the path of the file below the root of the resource, without `.lua` and with a dot between directories.")}, "any", "What the file returned."),
        };
        return globals;
    }

    std::string WriteLuaDefinitions(const ApiCatalog &catalog, Side side) {
        std::string out(kHeaderStart);
        out += SideName(side);
        out += kHeaderEnd;

        for (const ClassDesc *desc : catalog.Entries()) {
            if (ExistsIn(*desc, side)) {
                WriteDefinition(out, *desc, side);
            }
        }
        for (const FunctionDesc &function : LuaRuntimeGlobals()) {
            WriteFunction(out, function, function.name);
        }
        return out;
    }

    bool ExportLuaDefinitionsFromEnvironment(const std::vector<const ClassDesc *> &definitions, Side side, const char *variable) {
        const char *path = std::getenv(variable);
        if (path == nullptr || *path == '\0') {
            return true;
        }
        ApiCatalog catalog;
        for (const ClassDesc *desc : definitions) {
            catalog.Record(desc);
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << WriteLuaDefinitions(catalog, side);
        return static_cast<bool>(output.flush());
    }
} // namespace Framework::Scripting::Bind
