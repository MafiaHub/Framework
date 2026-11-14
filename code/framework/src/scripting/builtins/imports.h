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

#include <v8pp/convert.hpp>

#include <v8.h>

#include <string>

namespace Framework::Scripting {
    class ResourceManager;
    class Resource;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {

    // Global Imports: get(resourceName) -> every export registered by another running resource, declared once for
    // JavaScript and Lua. Each export arrives as Exports.get gives it (exports.h).
    class Imports final {
      public:
        static const Bind::Namespace &Definition();

        // Publishes Imports as a global in the languages of `languages`. In JavaScript the calls of the isolate answer
        // with `resourceManager` (Bind::SetCallResourceManager) when it is not null.
        static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All, ResourceManager *resourceManager = nullptr);

        /**
         * Build an object mapping every export name a resource registered from JavaScript to its
         * real value. Exports registered as copies (Resource::GetExportCopy) are left out. Assumes the caller has already validated that the resource's
         * values live in `isolate` (V8 values cannot cross isolates).
         * @param isolate V8 isolate owning the target resource's export values
         * @param context Context to build the result object in
         * @param resource Resource whose registered exports should be read
         * @return Object keyed by export name; empty object when there are no exports
         */
        static v8::Local<v8::Object> BuildImportsObject(v8::Isolate *isolate, v8::Local<v8::Context> context, const Resource *resource);

      private:
        // The functions of the definition.
        struct Script;
    };

} // namespace Framework::Scripting::Builtins
