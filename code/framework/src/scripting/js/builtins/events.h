#pragma once

#include <v8pp/convert.hpp>

#include <v8.h>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting::JS {

    class JSResourceManager;

    /**
     * JavaScript events system for inter-resource communication.
     * Provides Framework.events API:
     * - on(eventName, handler) - Register listener
     * - off(eventName, handler) - Remove listener
     * - emit(eventName, ...args) - Emit to all listeners
     * - emitTo(resourceName, eventName, ...args) - Targeted emit
     */
    class Events {
      public:
        /**
         * Register the Framework.events object in a context.
         * @param isolate V8 isolate
         * @param context Target context
         * @param frameworkObj Framework global object to attach to
         * @param resourceManager Resource manager for inter-resource communication
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            JSResourceManager *resourceManager);

        /**
         * Emit an event to all registered handlers for a specific resource.
         * @param isolate V8 isolate
         * @param context Target context
         * @param resourceName Resource to emit to
         * @param eventName Event name
         * @param args Event arguments
         */
        static void EmitToResource(v8::Isolate *isolate,
                                   v8::Local<v8::Context> context,
                                   const std::string &resourceName,
                                   const std::string &eventName,
                                   const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Emit an event to all registered handlers across all resources.
         * @param isolate V8 isolate
         * @param context Target context
         * @param eventName Event name
         * @param args Event arguments
         */
        static void EmitGlobal(v8::Isolate *isolate,
                               v8::Local<v8::Context> context,
                               const std::string &eventName,
                               const std::vector<v8::Local<v8::Value>> &args);

      private:
        // V8 callback implementations
        static void OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitToCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Event handler storage: resourceName -> eventName -> handlers
        static std::map<std::string, std::map<std::string, std::vector<v8::Global<v8::Function>>>> _handlers;
        static std::mutex _handlersMutex;

        // Resource manager reference (set during Register)
        static JSResourceManager *_resourceManager;
    };

} // namespace Framework::Scripting::JS
