/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "color.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

#include "../bind/doc.h"
#include "../value_transfer.h"

#include <lua.h>
#include <v8.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>

// Conventions every builtin here follows — namespace, throw idiom, registration
// shape — are documented in README.md next to this file. Match them when adding
// or editing a builtin.

namespace Framework::Scripting::Builtins {

    /**
     * Register the pure value types (Vector2/3/4, Quaternion, Color) on the target
     * object. Handle types (Entity, Player, TextLabel) and service APIs register
     * themselves — see README.md. Counterpart to UnregisterAll.
     */
    inline void RegisterValueTypes(v8::Isolate *isolate, v8::Local<v8::Object> target) {
        Vector2::Register(isolate, target);
        Vector3::Register(isolate, target);
        Vector4::Register(isolate, target);
        Quaternion::Register(isolate, target);
        Color::Register(isolate, target);
    }

    /**
     * Lua counterpart of RegisterValueTypes: publishes the same five classes as globals of the
     * state, which belongs to a runtime of side `side`. A failure, such as the state running out
     * of memory, is a Lua error: call it protected, or it is a panic.
     */
    inline void RegisterValueTypesLua(lua_State *L, Bind::Side side) {
        Vector2::RegisterLua(L, side);
        Vector3::RegisterLua(L, side);
        Vector4::RegisterLua(L, side);
        Quaternion::RegisterLua(L, side);
        Color::RegisterLua(L, side);
    }

    // Drops every builtin type's cached class wrapper for this isolate, then everything v8pp holds for it
    // (class registries, the objects they wrap, bound functions' data), after telling the callables and the
    // deferred results of the binding layer that the isolate goes (bind/callable.h). Call once, just before disposal:
    // a leftover entry leaks and, if the isolate address is later reused, resolves to a wrapper bound to
    // the dead isolate. Out-of-line so callers don't pull in the handle types' networking headers.
    //
    // Telling the callables completes the calls into the isolate that had not ended, each through its `done`,
    // which is host code and may run script in other runtimes: call this where that is safe to happen.
    void UnregisterAll(v8::Isolate *isolate);

    // Teaches ValueTransfer the builtin value types and handles, so they cross between resources as themselves rather
    // than as plain objects, and LuaTransfer the value types, with the same names and the same bytes, so they cross
    // between the two languages too. Process-wide and idempotent; it must run before a project registers its own
    // handle types, which the server scripting module's constructor sees to.
    void RegisterTransferTypes();

    /**
     * Teach ValueTransfer a handle class that wraps a network id (T::GetId(), T::GetClass(isolate), constructible from
     * the id), so it crosses between resources as itself. A project registers its own handles this way, in PostInit:
     * after the framework's, and each derived class after its base, because the newest registration is tried first.
     * Without it, a project handle derived from Player or Entity arrives as the framework base class.
     */
    template <typename T>
    void RegisterHandleTransfer(const char *name) {
        ValueTransfer::RegisterHostType(
            name,
            [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<uint8_t> &bytes) {
                T::GetClass(isolate);
                const auto handle = v8pp::class_<T>::unwrap_object(isolate, object);
                if (handle == nullptr) {
                    return false;
                }
                const uint64_t id = handle->GetId();
                bytes.resize(sizeof(id));
                std::memcpy(bytes.data(), &id, sizeof(id));
                return true;
            },
            [](v8::Isolate *isolate, v8::Local<v8::Context>, const std::vector<uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                uint64_t id = 0;
                std::memcpy(&id, bytes.data(), std::min(bytes.size(), sizeof(id)));
                T::GetClass(isolate);
                // A handle constructor throws when its entity is gone by the time the value arrives.
                try {
                    return v8pp::class_<T>::create_object(isolate, id);
                }
                catch (const std::exception &ex) {
                    isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, ex.what()).ToLocalChecked()));
                    return {};
                }
            });
    }

} // namespace Framework::Scripting::Builtins
