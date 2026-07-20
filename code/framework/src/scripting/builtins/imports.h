/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8pp/convert.hpp>

#include <v8.h>

#include <string>

namespace Framework::Scripting {
    class ResourceManager;
    class Resource;
}

namespace Framework::Scripting::Builtins {

    /**
     * Resource imports system for accessing exports from other resources.
     * Provides Framework.imports API:
     * - get(resourceName) - Returns exports from another resource
     */
    class Imports final {
      public:
        /**
         * Register the Framework.imports object in a context.
         * @param isolate V8 isolate
         * @param context Target context
         * @param frameworkObj Framework global object to attach to
         * @param resourceManager Resource manager for getting exports
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            ResourceManager *resourceManager);

        /**
         * Build an object mapping every registered export name of a resource to its
         * real value. Assumes the caller has already validated that the resource's
         * values live in `isolate` (V8 values cannot cross isolates).
         * @param isolate V8 isolate owning the target resource's export values
         * @param context Context to build the result object in
         * @param resource Resource whose registered exports should be read
         * @return Object keyed by export name; empty object when there are no exports
         */
        static v8::Local<v8::Object> BuildImportsObject(v8::Isolate *isolate,
                                                        v8::Local<v8::Context> context,
                                                        const Resource *resource);

      private:
        // V8 callback implementations
        static void GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static ResourceManager *_resourceManager;
    };

} // namespace Framework::Scripting::Builtins
