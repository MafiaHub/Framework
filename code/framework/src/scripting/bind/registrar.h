/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "class.h"
#include "namespace.h"
#include "types.h"

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

struct lua_State;

namespace Framework::Scripting {
    class LuaRuntime;
} // namespace Framework::Scripting

namespace Framework::Scripting::Bind {
    // The languages a builtin is registered in.
    enum class Languages : uint8_t {
        None       = 0,
        JavaScript = 1,
        Lua        = 2,
        All        = JavaScript | Lua,
    };

    constexpr Languages operator|(Languages left, Languages right) {
        return static_cast<Languages>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr Languages operator&(Languages left, Languages right) {
        return static_cast<Languages>(static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    // Whether `set` has every language of `languages`.
    constexpr bool Includes(Languages set, Languages languages) {
        return languages != Languages::None && (set & languages) == languages;
    }

    // What a registrar leaves behind for Lua: the definitions to publish in each state, in the order they were
    // registered. Applied to a state as it is created, with the side of its runtime.
    class LuaBindings final {
      public:
        using Setup = std::function<void(lua_State *L, Side runtime)>;

        void Add(Setup setup) {
            _setups.push_back(std::move(setup));
        }

        // Publishes everything in the state of `runtime`. Runs unprotected, as MaterialiseLua: call it from the
        // setup callback of the Lua engine, which protects it.
        void Apply(LuaRuntime &runtime) const;
        void Apply(lua_State *L, Side runtime) const;

        bool Empty() const {
            return _setups.empty();
        }

      private:
        std::vector<Setup> _setups;
    };

    // What the Register of a builtin is given: it registers the builtin in both languages through it. JavaScript is
    // registered at once, in the isolate the registrar was built for; a registrar built without one registers no
    // JavaScript. Lua is recorded (Lua()) and applied to each Lua state as it is created, because a state is made
    // per resource, long after the builtins are registered.
    class Registrar final {
      public:
        // Records Lua only.
        Registrar() = default;

        // Registers JavaScript in `isolate`, on `target`, with the members of a runtime of side `side`, and records
        // Lua. Call with `context` entered.
        Registrar(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Side side);

        // The same, with the side the isolate was set up with (ScriptingSide).
        Registrar(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target);

        // Whether JavaScript is registered for `languages`: the registrar has an isolate and `languages` has
        // JavaScript. A builtin asks it before its own JavaScript work, such as what it keeps for the isolate.
        bool HasJavaScript(Languages languages = Languages::All) const {
            return _isolate != nullptr && Includes(languages, Languages::JavaScript);
        }

        v8::Isolate *Isolate() const {
            return _isolate;
        }

        v8::Local<v8::Context> Context() const {
            return _context;
        }

        // The object the globals are set on.
        v8::Local<v8::Object> Target() const {
            return _target;
        }

        // The side of the isolate. Lua takes the side of each runtime instead.
        Side JavaScriptSide() const {
            return _side;
        }

        // The namespace as a global. Returns the JavaScript object, or an empty handle when none was made.
        v8::Local<v8::Object> Global(const Namespace &definition, Languages languages = Languages::All, const Namespace::RuntimeConstants &constants = {});

        // The same, with the values of the runtime constants of each side.
        v8::Local<v8::Object> Global(const Namespace &definition, Languages languages, std::function<Namespace::RuntimeConstants(Side)> constants);

        // The class as a global: its constructor and its statics. In JavaScript the class is the one T::GetClass
        // keeps for the isolate.
        template <typename T>
        void Global(const Class<T> &definition, Languages languages = Languages::All) {
            if (HasJavaScript(languages)) {
                v8pp::class_<T> &cls = T::GetClass(_isolate);
                _target->Set(_context, v8pp::to_v8(_isolate, definition.Describe().name), cls.js_function_template()->GetFunction(_context).ToLocalChecked()).Check();
            }
            if (Includes(languages, Languages::Lua)) {
                _lua.Add([definition](lua_State *L, Side runtime) {
                    definition.RegisterLuaGlobal(L, runtime);
                });
            }
        }

        // The class known to the runtime without being a global: what a value of another class reads as. Only Lua
        // has anything to do: JavaScript builds a class the first time T::GetClass is asked for it.
        template <typename T>
        void Type(const Class<T> &definition, Languages languages = Languages::All) {
            if (Includes(languages, Languages::Lua)) {
                _lua.Add([definition](lua_State *L, Side runtime) {
                    definition.MaterialiseLua(L, runtime);
                });
            }
        }

        // What is recorded for each Lua state.
        const LuaBindings &Lua() const {
            return _lua;
        }

      private:
        v8::Isolate *_isolate = nullptr;
        v8::Local<v8::Context> _context;
        v8::Local<v8::Object> _target;
        Side _side = Side::Shared;
        LuaBindings _lua;
    };
} // namespace Framework::Scripting::Bind
