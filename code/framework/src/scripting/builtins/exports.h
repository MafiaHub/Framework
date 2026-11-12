/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/namespace.h"
#include "../transferred_value.h"

#include <utils/result.h>

#include <v8.h>
#include <v8pp/convert.hpp>

#include <string>
#include <string_view>

namespace Framework::Scripting {
    class LuaRuntime;
    class Resource;
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {

    // Global Exports: register(name, value) and get(resourceName, exportName), declared once for JavaScript and Lua.
    //
    // What a JavaScript script registers stays a value of its isolate (Resource::RegisterExport), and a JavaScript
    // importer gets it as it always did: the value itself in the same isolate, a copy whose functions are function
    // references from another one, a method running on the owner's object. What a Lua script registers is copied
    // when it is registered, its functions as callables, and every importer gets a rebuild of that copy. A Lua
    // importer gets a copy of a JavaScript export made the same way. A function that crossed as a callable is
    // called on nothing: no `this`, no receiver.
    class Exports final {
      public:
        static const Bind::Namespace &Definition();

        // Exposed as `Exports`, not `exports`: Node's CommonJS module scope would shadow the latter.
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager);

        static void RegisterLua(LuaRuntime &runtime);

        // A copy of an export the resource registered from JavaScript, made inside the isolate that holds it, for
        // an importer that is not JavaScript: its functions cross as callables. The error says why there is none:
        // the isolate cannot be entered, the name is not registered there, or where in the value the copy gave up.
        static Utils::Result<TransferredValue, std::string> CopyJavaScriptExport(const Resource &resource, std::string_view name);

      private:
        // The functions of the definition.
        struct Script;
    };

} // namespace Framework::Scripting::Builtins
