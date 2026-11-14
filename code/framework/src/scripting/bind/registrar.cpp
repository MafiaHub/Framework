/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "registrar.h"

#include "../lua/lua_runtime.h"
#include "../scripting_catalog.h"

namespace Framework::Scripting::Bind {
    void LuaBindings::Apply(LuaRuntime &runtime) const {
        Apply(runtime.State(), runtime.Options().side);
    }

    void LuaBindings::Apply(lua_State *L, Side runtime) const {
        for (const Setup &setup : _setups) {
            setup(L, runtime);
        }
    }

    Registrar::Registrar(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Side side): _isolate(isolate), _context(context), _target(target), _side(side) {}

    Registrar::Registrar(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target): Registrar(isolate, context, target, ScriptingSide(isolate)) {}

    v8::Local<v8::Object> Registrar::Global(const Namespace &definition, Languages languages, const Namespace::RuntimeConstants &constants) {
        return Global(definition, languages, [constants](Side) {
            return constants;
        });
    }

    v8::Local<v8::Object> Registrar::Global(const Namespace &definition, Languages languages, std::function<Namespace::RuntimeConstants(Side)> constants) {
        v8::Local<v8::Object> object;
        if (HasJavaScript(languages)) {
            object = definition.MaterialiseV8(_isolate, _context, _target, GetScriptingCatalog(_isolate), _side, constants(_side));
        }
        if (Includes(languages, Languages::Lua)) {
            _lua.Add([definition, constants](lua_State *L, Side runtime) {
                definition.RegisterLuaGlobal(L, runtime, constants(runtime));
            });
        }
        return object;
    }
} // namespace Framework::Scripting::Bind
