#pragma once

#include "events_reserved.h"

#include <v8pp/convert.hpp>
#include <v8.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting {

    class ResourceManager;

    /**
     * Handler entry with metadata
     */
    struct EventHandler {
        v8::Global<v8::Function> callback;
        std::string resourceName;
        bool once;
    };

    /**
     * JavaScript Events system - global event bus with async support.
     *
     * Provides Events global API:
     * - on(eventName, handler) - Register listener, returns unsubscribe function
     * - once(eventName, handler) - One-time listener
     * - off(eventName, handler) - Remove listener
     * - emit(eventName, ...args) - Emit globally, returns Promise
     * - emitTo(resourceName, eventName, ...args) - Targeted emit
     * - onLocal(eventName, handler) - Resource-local listener
     * - emitLocal(eventName, ...args) - Resource-local emit
     * - listenerCount(eventName) - Get handler count
     */
    class Events {
      public:
        /**
         * Register the Events global object in a context.
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> global,
                            ResourceManager *resourceManager);

        /**
         * Emit a reserved event (framework-only, bypasses protection).
         * Returns a Promise that resolves when all handlers complete.
         */
        static v8::Local<v8::Promise> EmitReserved(v8::Isolate *isolate,
                                                    v8::Local<v8::Context> context,
                                                    const std::string &eventName,
                                                    const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Clean up all handlers for a resource (called on resource stop).
         */
        static void CleanupResource(const std::string &resourceName);

        /**
         * Get count of handlers for an event.
         */
        static size_t GetListenerCount(const std::string &eventName);

      private:
        // V8 callbacks
        static void OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnceCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitToCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ListenerCountCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Internal registration helper
        static void RegisterHandler(v8::Isolate *isolate,
                                   ResourceManager *manager,
                                   const std::string &eventName,
                                   v8::Local<v8::Function> handler,
                                   bool once);

        // Internal emit helper - returns Promise
        static v8::Local<v8::Promise> EmitInternal(v8::Isolate *isolate,
                                                   v8::Local<v8::Context> context,
                                                   const std::string &eventName,
                                                   const std::vector<v8::Local<v8::Value>> &args,
                                                   const std::string &targetResource = "");

        // Get handlers map for current isolate (creates if needed)
        static std::map<std::string, std::vector<EventHandler>> &GetGlobalHandlers(v8::Isolate *isolate);
        static std::map<std::string, std::map<std::string, std::vector<EventHandler>>> &GetLocalHandlers(v8::Isolate *isolate);

        // Per-isolate handlers storage
        static std::map<v8::Isolate *, std::map<std::string, std::vector<EventHandler>>> _isolateGlobalHandlers;
        static std::map<v8::Isolate *, std::map<std::string, std::map<std::string, std::vector<EventHandler>>>> _isolateLocalHandlers;

        static std::mutex _handlersMutex;
        static ResourceManager *_resourceManager;
        static v8::Isolate *_currentIsolate;
    };

} // namespace Framework::Scripting
