/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/namespace.h"
#include "../bind/registrar.h"
#include "../transferred_value.h"

#include <utils/result.h>

#include <v8.h>
#include <v8pp/convert.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Framework::Scripting {
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
    // importer gets a copy of a JavaScript export made the same way.
    //
    // A function that crossed as a callable is called on nothing: no `this`, no receiver. In Lua a function that
    // arrived as a member of a table takes a call on that table for a method call and drops the table
    // (Bind::LuaNoteHolder), so `api:deposit(5)` and `api.deposit(5)` are the same call. The JavaScript function
    // behind it still runs with `this` undefined: a JavaScript export whose methods use `this` does not work from
    // Lua.
    class Exports final {
      public:
        static const Bind::Namespace &Definition();

        // Exposed as `Exports`, not `exports`: Node's CommonJS module scope would shadow the latter.
        // Publishes Exports as a global in the languages of `languages`. In JavaScript the calls of the isolate answer
        // with `resourceManager` (Bind::SetCallResourceManager) when it is not null.
        static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All, ResourceManager *resourceManager = nullptr);

        using Copies = std::vector<std::pair<std::string, TransferredValue>>;

        // Copies of the exports `names` that the resource registered from JavaScript, in that order, made inside
        // the isolate that holds them, which is entered once: for an importer that is not JavaScript. Their
        // functions cross as callables. The error is "export '<name>' of resource '<resource>': <why>" for the
        // first that has no copy: the isolate cannot be entered, the name is not registered there, or where in
        // the value the copy gave up.
        static Utils::Result<Copies, std::string> CopyJavaScriptExports(const Resource &resource, const std::vector<std::string> &names);

        // The same for one export.
        static Utils::Result<TransferredValue, std::string> CopyJavaScriptExport(const Resource &resource, std::string_view name);

      private:
        // The functions of the definition.
        struct Script;
    };

} // namespace Framework::Scripting::Builtins
