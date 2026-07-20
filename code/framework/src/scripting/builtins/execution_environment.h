/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8pp/module.hpp>

namespace Framework::Scripting::Builtins {

    /**
     * JavaScript ExecutionEnvironment API - provides runtime environment information.
     *
     * Exposes an ExecutionEnvironment global object:
     * - ExecutionEnvironment.isClient - true if running on the client side (read-only)
     * - ExecutionEnvironment.isServer - true if running on the server side (read-only)
     */
    class ExecutionEnvironment final {
      public:
        /**
         * Register the ExecutionEnvironment object on the target object.
         * @param isolate V8 isolate
         * @param context Target context
         * @param target Object to attach ExecutionEnvironment to (e.g., the global root)
         * @param isClient true if this is the client side, false for server
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> target,
                            bool isClient);
    };

} // namespace Framework::Scripting::Builtins
