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
#include <spdlog/spdlog.h>

#include <string>

namespace Framework::Scripting {

    class ResourceManager;

    /**
     * Console override that routes to Framework logger.
     * Overrides global console:
     * - console.log() -> Framework logger (info level) with resource prefix
     * - console.warn() -> Framework logger (warning level)
     * - console.error() -> Framework logger (error level)
     * - console.debug() -> Framework logger (debug level)
     */
    class Console final {
      public:
        /**
         * Register the console override in a context.
         * @param isolate V8 isolate
         * @param context Target context
         * @param resourceManager Resource manager for getting current resource context
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            ResourceManager *resourceManager);

        // Helper to format arguments as a string (public for testing)
        static std::string FormatArgs(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args);

        // Format a single V8 value as a string (public for testing)
        static std::string FormatValue(v8::Isolate *isolate, v8::Local<v8::Value> value);

      private:
        // V8 callback implementations
        static void LogCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void WarnCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ErrorCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void DebugCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Helper to log at a specific level
        static void LogWithLevel(const v8::FunctionCallbackInfo<v8::Value> &args, spdlog::level::level_enum level);

    };

} // namespace Framework::Scripting
