/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/bind/descriptors.h"
#include "scripting/bind/lua_class.h"
#include "scripting/bind/lua_definitions.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The parity gate: what the JavaScript catalog of a side documents, held against a Lua state of that side member
// by member, and the Lua definition file held against the same state. The server and the client module each bring
// their catalog, their state and their exemptions (lua_parity_ut.h, lua_client_parity_ut.h).
namespace LuaParity {
    using Framework::Scripting::Bind::ApiCatalog;
    using Framework::Scripting::Bind::ClassDesc;
    using Framework::Scripting::Bind::Side;
    namespace BindDetail = Framework::Scripting::Bind::Detail;

    // What JavaScript has and Lua has not, by design: "<global>", "<Symbol>.<member>" or "<Class>.new", with the
    // reason. Nothing else may be missing, and an entry that excuses nothing fails the gate too.
    using Exemptions = std::map<std::string, std::string>;

    struct Report final {
        int symbols   = 0; // global objects and classes of the catalog, and its data types a runtime has
        int members   = 0; // functions, properties, constructors and global functions looked for in Lua
        int exempted  = 0;
        int dataTypes = 0; // skipped: the shape of an option object or of a payload, which no runtime holds
        std::set<std::string> missing;
        std::set<std::string> stale; // exemptions that excuse nothing
    };

    inline std::string Join(const std::set<std::string> &names) {
        std::string text;
        for (const std::string &name : names) {
            text += (text.empty() ? "" : ", ") + name;
        }
        return text;
    }

    // The builtin named `name` that a runtime of `side` has, or nullptr.
    inline const ClassDesc *FindBuiltin(const std::vector<const ClassDesc *> &builtins, const std::string &name, Side side) {
        for (const ClassDesc *desc : builtins) {
            if (desc->name == name && ExistsIn(*desc, side)) {
                return desc;
            }
        }
        return nullptr;
    }

    // The type of the global `name`.
    inline int GlobalType(lua_State *L, const std::string &name) {
        const int type = lua_getglobal(L, name.c_str());
        lua_pop(L, 1);
        return type;
    }

    // The type of what a script reads as `<global>.<field>`, LUA_TNONE when the global is not a table.
    inline int GlobalFieldType(lua_State *L, const std::string &global, const std::string &field) {
        int type = LUA_TNONE;
        if (lua_getglobal(L, global.c_str()) == LUA_TTABLE) {
            type = lua_getfield(L, -1, field.c_str());
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return type;
    }

    // The type of what the class table of `definition` has under `field`: a static, or `new`. A class has its
    // table in a state whether or not a global names it.
    inline int ClassFieldType(lua_State *L, const BindDetail::LuaClass &definition, const std::string &field) {
        int type = LUA_TNONE;
        if (BindDetail::LuaPushClassTable(L, definition)) {
            type = lua_getfield(L, -1, field.c_str());
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return type;
    }

    // The type of what the metatable of the objects of `definition` has under `event`: "__add".
    inline int MetaType(lua_State *L, const BindDetail::LuaClass &definition, const std::string &event) {
        int type = LUA_TNONE;
        if (BindDetail::LuaPushInstanceMetatable(L, definition)) {
            lua_pushstring(L, event.c_str());
            type = lua_rawget(L, -2);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return type;
    }

    // Whether calling the class table of `definition` constructs: it has a constructor, and its table a __call.
    inline bool IsCallable(lua_State *L, const BindDetail::LuaClass &definition) {
        bool callable = false;
        if (BindDetail::LuaPushClassTable(L, definition) && lua_getmetatable(L, -1) != 0) {
            lua_pushstring(L, "__call");
            callable = lua_rawget(L, -2) == LUA_TFUNCTION;
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
        return callable && definition.constructor.has_value();
    }

    // The metamethod behind an operator as a script writes it between its operands, "" for none: "==" is "__eq".
    inline std::string EventOfSymbol(std::string_view symbol, bool unary) {
        static const std::map<std::string_view, const char *> binary = {{"+", "__add"}, {"-", "__sub"}, {"*", "__mul"}, {"/", "__div"}, {"==", "__eq"}, {"<", "__lt"}, {"<=", "__le"}, {"..", "__concat"}};
        if (unary) {
            return symbol == "-" ? "__unm" : symbol == "#" ? "__len" : "";
        }
        const auto it = binary.find(symbol);
        return it != binary.end() ? it->second : "";
    }

    class Gate final {
      public:
        Gate(lua_State *L, Side side, const std::vector<const ClassDesc *> &builtins, const Exemptions &exemptions): _L(L), _side(side), _builtins(builtins), _exemptions(exemptions) {}

        // Holds every symbol and global function of a normalised JavaScript catalog against the state.
        Report Check(const nlohmann::json &catalog) {
            _report = {};
            _used.clear();
            for (const nlohmann::json &symbol : catalog.value("symbols", nlohmann::json::array())) {
                CheckSymbol(symbol);
            }
            for (const nlohmann::json &function : catalog.value("functions", nlohmann::json::array())) {
                const std::string name = function.value("name", std::string());
                Expect(name, GlobalType(_L, name) == LUA_TFUNCTION, "global function");
            }
            for (const auto &[key, reason] : _exemptions) {
                if (!_used.contains(key)) {
                    _report.stale.insert(key + " (names nothing the catalog documents)");
                }
            }
            return _report;
        }

      private:
        // One thing JavaScript documents: there in Lua, or exempt. An exemption for what is there is stale.
        void Expect(const std::string &key, bool present, const char *what) {
            ++_report.members;
            if (_exemptions.contains(key)) {
                _used.insert(key);
                if (present) {
                    _report.stale.insert(key + " (Lua has it)");
                }
                else {
                    ++_report.exempted;
                }
                return;
            }
            if (!present) {
                _report.missing.insert(key + " (" + what + ")");
            }
        }

        void CheckSymbol(const nlohmann::json &symbol) {
            const std::string name = symbol.value("name", std::string());
            const std::string kind = symbol.value("kind", std::string());
            if (kind == "globalObject") {
                ++_report.symbols;
                CheckNamespace(symbol, name);
                return;
            }
            const ClassDesc *desc                  = FindBuiltin(_builtins, name, _side);
            const BindDetail::LuaClass *definition = desc != nullptr ? BindDetail::LuaClassDescribedBy(*desc) : nullptr;
            if (kind == "dataType" && definition == nullptr) {
                ++_report.dataTypes;
                return;
            }
            ++_report.symbols;
            if (kind == "constructor") {
                Expect(name, GlobalType(_L, name) == LUA_TTABLE, "global");
            }
            if (definition == nullptr) {
                _report.missing.insert(name + " (no class of this name is defined for Lua)");
                return;
            }
            CheckClass(symbol, name, *definition);
        }

        void CheckNamespace(const nlohmann::json &symbol, const std::string &name) {
            Expect(name, GlobalType(_L, name) == LUA_TTABLE, "global");
            for (const nlohmann::json &function : symbol.value("functions", nlohmann::json::array())) {
                const std::string member = function.value("name", std::string());
                Expect(name + "." + member, GlobalFieldType(_L, name, member) == LUA_TFUNCTION, "function");
            }
            for (const nlohmann::json &property : symbol.value("properties", nlohmann::json::array())) {
                const std::string member = property.value("name", std::string());
                const int type           = GlobalFieldType(_L, name, member);
                Expect(name + "." + member, type != LUA_TNONE && type != LUA_TNIL, "property");
                // A namespace of Lua refuses every write.
                if (!property.value("readonly", false)) {
                    _report.missing.insert(name + "." + member + " (a script of JavaScript may write it; Lua has constants only)");
                }
            }
        }

        void CheckClass(const nlohmann::json &symbol, const std::string &name, const BindDetail::LuaClass &definition) {
            using BindDetail::LuaInstanceMember;
            for (const nlohmann::json &function : symbol.value("functions", nlohmann::json::array())) {
                const std::string member = function.value("name", std::string());
                if (function.value("static", false)) {
                    Expect(name + "." + member, ClassFieldType(_L, definition, member) == LUA_TFUNCTION, "static");
                }
                else {
                    Expect(name + "." + member, BindDetail::LuaFindInstanceMember(_L, definition, member) == LuaInstanceMember::Method, "method");
                }
            }
            for (const nlohmann::json &property : symbol.value("properties", nlohmann::json::array())) {
                const std::string member = property.value("name", std::string());
                if (property.value("static", false)) {
                    const int type = ClassFieldType(_L, definition, member);
                    Expect(name + "." + member, type != LUA_TNONE && type != LUA_TNIL, "static property");
                    continue;
                }
                const LuaInstanceMember found = BindDetail::LuaFindInstanceMember(_L, definition, member);
                const bool writable           = !property.value("readonly", false);
                Expect(name + "." + member, found == (writable ? LuaInstanceMember::Property : LuaInstanceMember::ReadOnlyProperty), writable ? "property with a setter" : "read-only property");
            }
            // Every class table has `new`, which refuses when the class declares no constructor.
            const bool constructible = symbol.contains("constructor") && symbol["constructor"].is_object();
            if (constructible) {
                Expect(name + ".new", definition.constructor.has_value() && ClassFieldType(_L, definition, "new") == LUA_TFUNCTION, "constructor");
            }
            else if (definition.constructor.has_value()) {
                _report.missing.insert(name + ".new (Lua constructs what JavaScript cannot)");
            }
        }

        lua_State *_L;
        Side _side;
        const std::vector<const ClassDesc *> &_builtins;
        const Exemptions &_exemptions;
        Report _report;
        std::set<std::string> _used;
    };

    // What a definition file declares, and what of it a state lacks.
    struct Declared final {
        std::set<std::string> globals; // tables and functions
        std::set<std::string> tables;  // classes and namespaces, global or not
        int members   = 0;             // functions and fields of a table
        int operators = 0;             // annotated, and named in a description
        int callable  = 0;             // classes with a call signature
        std::set<std::string> missing;
    };

    // Reads a definition file as WriteLuaDefinitions writes it and looks for all of it in the state.
    inline Declared CheckDefinitions(const std::string &text, lua_State *L, Side side, const std::vector<const ClassDesc *> &builtins) {
        using BindDetail::LuaInstanceMember;
        Declared declared;
        std::string current; // the class the annotations being read belong to
        std::set<std::string> local;
        std::vector<std::string> pendingOperators; // named in a description, for the class it leads to

        const auto classOf = [&](const std::string &name) -> const BindDetail::LuaClass * {
            const ClassDesc *desc = FindBuiltin(builtins, name, side);
            return desc != nullptr ? BindDetail::LuaClassDescribedBy(*desc) : nullptr;
        };
        const auto word = [](std::string_view text) {
            size_t length = 0;
            while (length < text.size() && (std::isalnum(static_cast<unsigned char>(text[length])) != 0 || text[length] == '_')) {
                ++length;
            }
            return std::string(text.substr(0, length));
        };

        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);) {
            const std::string_view view(line);
            if (view.starts_with("---@class ")) {
                current = word(view.substr(10));
                declared.tables.insert(current);
                const BindDetail::LuaClass *definition = classOf(current);
                for (const std::string &event : pendingOperators) {
                    ++declared.operators;
                    if (definition == nullptr || MetaType(L, *definition, event) != LUA_TFUNCTION) {
                        declared.missing.insert(current + " " + event + " (operator named in the description)");
                    }
                }
                pendingOperators.clear();
            }
            else if (view.starts_with("---@field ") && !current.empty()) {
                const std::string field = word(view.substr(10));
                ++declared.members;
                if (const BindDetail::LuaClass *definition = classOf(current)) {
                    const bool readonly = view.find(" " + std::string(Framework::Scripting::Bind::kReadOnlyMarker)) != std::string_view::npos;
                    if (BindDetail::LuaFindInstanceMember(L, *definition, field) != (readonly ? LuaInstanceMember::ReadOnlyProperty : LuaInstanceMember::Property)) {
                        declared.missing.insert(current + "." + field + (readonly ? " (read-only property)" : " (property with a setter)"));
                    }
                }
                else if (const int type = GlobalFieldType(L, current, field); type == LUA_TNONE || type == LUA_TNIL) {
                    declared.missing.insert(current + "." + field + " (constant)");
                }
            }
            else if (view.starts_with("---@operator ") && !current.empty()) {
                const std::string event                = "__" + word(view.substr(13));
                const BindDetail::LuaClass *definition = classOf(current);
                ++declared.operators;
                if (definition == nullptr || MetaType(L, *definition, event) != LUA_TFUNCTION) {
                    declared.missing.insert(current + " " + event + " (operator)");
                }
            }
            else if (view.starts_with("---Also has the operators ") && !pendingOperators.empty()) {
                declared.missing.insert("operators described twice before a class");
            }
            else if (view.starts_with("---Also has the operators ")) {
                // `left symbol right`, or `symbol operand`, each between backticks. Held until the class is named.
                for (size_t open = view.find('`'); open != std::string_view::npos; open = view.find('`', open + 1)) {
                    const size_t close = view.find('`', open + 1);
                    if (close == std::string_view::npos) {
                        break;
                    }
                    const std::string_view written = view.substr(open + 1, close - open - 1);
                    const size_t first             = written.find(' ');
                    const size_t last              = written.rfind(' ');
                    std::string event;
                    if (first == std::string_view::npos) {
                        size_t length = 0;
                        while (length < written.size() && std::isalnum(static_cast<unsigned char>(written[length])) == 0) {
                            ++length;
                        }
                        event = EventOfSymbol(written.substr(0, length), true);
                    }
                    else if (last > first) {
                        event = EventOfSymbol(written.substr(first + 1, last - first - 1), false);
                    }
                    pendingOperators.push_back(event.empty() ? std::string(written) : event);
                    open = close;
                }
            }
            else if (view.starts_with("---@overload ") && !current.empty()) {
                const BindDetail::LuaClass *definition = classOf(current);
                ++declared.callable;
                if (definition == nullptr || !IsCallable(L, *definition)) {
                    declared.missing.insert(current + " (callable class)");
                }
            }
            else if (view.ends_with(" = nil") && view.find('.') != std::string_view::npos) {
                const std::string owner                = word(view);
                const std::string field                = word(view.substr(owner.size() + 1));
                const BindDetail::LuaClass *definition = classOf(owner);
                ++declared.members;
                if (const int type = definition != nullptr ? ClassFieldType(L, *definition, field) : LUA_TNONE; type == LUA_TNONE || type == LUA_TNIL) {
                    declared.missing.insert(owner + "." + field + " (static property)");
                }
            }
            else if (view.ends_with(" = {}")) {
                const bool isLocal     = view.starts_with("local ");
                const std::string name = word(view.substr(isLocal ? 6 : 0));
                if (isLocal) {
                    local.insert(name);
                    const BindDetail::LuaClass *definition = classOf(name);
                    if (definition == nullptr || ClassFieldType(L, *definition, "new") != LUA_TFUNCTION) {
                        declared.missing.insert(name + " (class)");
                    }
                    if (GlobalType(L, name) != LUA_TNIL) {
                        declared.missing.insert(name + " (declared as no global, and is one)");
                    }
                }
                else {
                    declared.globals.insert(name);
                    if (GlobalType(L, name) != LUA_TTABLE) {
                        declared.missing.insert(name + " (global)");
                    }
                }
            }
            else if (view.starts_with("function ")) {
                const std::string owner = word(view.substr(9));
                const size_t after      = 9 + owner.size();
                if (after >= view.size() || view[after] == '(') {
                    declared.globals.insert(owner);
                    if (GlobalType(L, owner) != LUA_TFUNCTION) {
                        declared.missing.insert(owner + " (global function)");
                    }
                    continue;
                }
                const std::string member               = word(view.substr(after + 1));
                const BindDetail::LuaClass *definition = classOf(owner);
                ++declared.members;
                bool present = false;
                if (view[after] == ':') {
                    present = definition != nullptr && BindDetail::LuaFindInstanceMember(L, *definition, member) == LuaInstanceMember::Method;
                }
                else if (definition != nullptr) {
                    present = ClassFieldType(L, *definition, member) == LUA_TFUNCTION && (member != "new" || definition->constructor.has_value());
                }
                else {
                    present = GlobalFieldType(L, owner, member) == LUA_TFUNCTION;
                }
                if (!present) {
                    declared.missing.insert(owner + view[after] + member + " (function)");
                }
            }
        }
        return declared;
    }

    // Whether a definition file is Lua a language server can read: "" when it compiles, the error otherwise.
    inline std::string CompileError(const std::string &text) {
        lua_State *L = luaL_newstate();
        std::string error;
        if (luaL_loadbufferx(L, text.data(), text.size(), "=definitions", "t") != LUA_OK) {
            error = lua_tostring(L, -1);
        }
        lua_close(L);
        return error;
    }

    // What separates the globals the JavaScript catalog documents from the definitions Lua is set up from: a
    // global object or a class of the catalog that no definition is listed for, and a listed namespace or global
    // class the catalog does not have. A builtin added to one language alone shows here.
    inline std::set<std::string> OneSided(const nlohmann::json &catalog, const std::vector<const ClassDesc *> &builtins, Side side) {
        std::set<std::string> documented;
        for (const nlohmann::json &symbol : catalog.value("symbols", nlohmann::json::array())) {
            if (symbol.value("kind", std::string()) != "dataType") {
                documented.insert(symbol.value("name", std::string()));
            }
        }
        std::set<std::string> names;
        for (const std::string &name : documented) {
            if (FindBuiltin(builtins, name, side) == nullptr) {
                names.insert(name + " (JavaScript only)");
            }
        }
        for (const ClassDesc *desc : builtins) {
            if (ExistsIn(*desc, side) && (desc->isNamespace || desc->global) && !documented.contains(desc->name)) {
                names.insert(desc->name + " (Lua only)");
            }
        }
        return names;
    }

    // The globals of a Lua state with every standard library open: what a script has without the framework.
    inline std::set<std::string> StandardGlobals() {
        std::set<std::string> names;
        lua_State *L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushglobaltable(L);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            lua_pop(L, 1);
            if (lua_type(L, -1) == LUA_TSTRING) {
                names.insert(lua_tostring(L, -1));
            }
        }
        lua_close(L);
        return names;
    }

    // The globals of the state that are not Lua's own and that the definition file does not declare.
    inline std::set<std::string> UndeclaredGlobals(lua_State *L, const std::set<std::string> &declared) {
        const std::set<std::string> standard = StandardGlobals();
        std::set<std::string> names;
        lua_pushglobaltable(L);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            lua_pop(L, 1);
            if (lua_type(L, -1) == LUA_TSTRING) {
                const std::string name = lua_tostring(L, -1);
                if (!standard.contains(name) && !declared.contains(name)) {
                    names.insert(name);
                }
            }
        }
        lua_pop(L, 1);
        return names;
    }

    // --- The committed definition files ---

    inline bool UpdateRequested() {
        const char *value = std::getenv("FW_UPDATE_GOLDEN");
        return value && *value && std::strcmp(value, "0") != 0;
    }

    inline std::string DefinitionsPath(const char *file) {
        return std::string(FW_TEST_LUA_TYPES_DIR) + "/" + file;
    }

    // The file, or "" when it cannot be read.
    inline std::string ReadDefinitions(const char *file) {
        std::ifstream input(DefinitionsPath(file), std::ios::binary);
        std::stringstream content;
        content << input.rdbuf();
        return input ? content.str() : std::string();
    }

    inline bool WriteDefinitions(const char *file, const std::string &text) {
        std::ofstream output(DefinitionsPath(file), std::ios::binary | std::ios::trunc);
        output << text;
        return static_cast<bool>(output.flush());
    }

    // The first line two texts differ on, or "" when they are the same.
    inline std::string FirstDifference(const std::string &committed, const std::string &generated) {
        if (committed == generated) {
            return {};
        }
        if (committed.empty()) {
            return "no committed file; run this module once with FW_UPDATE_GOLDEN=1 to write it";
        }
        std::istringstream left(committed), right(generated);
        std::string leftLine, rightLine;
        for (int number = 1;; ++number) {
            const bool hasLeft  = static_cast<bool>(std::getline(left, leftLine));
            const bool hasRight = static_cast<bool>(std::getline(right, rightLine));
            if (!hasLeft || !hasRight || leftLine != rightLine) {
                return "line " + std::to_string(number) + ": committed '" + (hasLeft ? leftLine : "<end>") + "', generated '" + (hasRight ? rightLine : "<end>") + "'; run this module with FW_UPDATE_GOLDEN=1 to take the change";
            }
        }
    }

    inline std::string Generate(const std::vector<const ClassDesc *> &builtins, Side side) {
        ApiCatalog catalog;
        for (const ClassDesc *desc : builtins) {
            catalog.Record(desc);
        }
        return WriteLuaDefinitions(catalog, side);
    }
} // namespace LuaParity
