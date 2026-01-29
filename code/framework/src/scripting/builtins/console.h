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

      private:
        // V8 callback implementations
        static void LogCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void WarnCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ErrorCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void DebugCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Helper to log at a specific level
        static void LogWithLevel(const v8::FunctionCallbackInfo<v8::Value> &args, spdlog::level::level_enum level);

        // Helper to format arguments as a string
        static std::string FormatArgs(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args);

        static ResourceManager *_resourceManager;
    };

} // namespace Framework::Scripting
