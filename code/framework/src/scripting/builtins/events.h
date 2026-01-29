#pragma once

#include "events_reserved.h"

#include <v8pp/convert.hpp>
#include <v8.h>

#include <atomic>
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
        v8::Isolate *isolate = nullptr;  // Track which isolate owns this handler
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
        Events() = default;
        ~Events();

        // Non-copyable
        Events(const Events &) = delete;
        Events &operator=(const Events &) = delete;

        /**
         * Register the Events global object in a context.
         */
        void Register(v8::Isolate *isolate,
                     v8::Local<v8::Context> context,
                     v8::Local<v8::Object> global,
                     ResourceManager *resourceManager);

        /**
         * Emit a reserved event (framework-only, bypasses protection).
         * Returns a Promise that resolves when all handlers complete.
         */
        v8::Local<v8::Promise> EmitReserved(v8::Isolate *isolate,
                                            v8::Local<v8::Context> context,
                                            const std::string &eventName,
                                            const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Clean up all handlers for a resource (called on resource stop).
         */
        void CleanupResource(const std::string &resourceName);

        /**
         * Clear all handlers (called on shutdown).
         */
        void ClearAll();

        /**
         * Get count of handlers for an event.
         */
        size_t GetListenerCount(const std::string &eventName);

      private:
        /**
         * Context data passed to V8 callbacks via External.
         * Contains both the Events instance and ResourceManager.
         * The valid flag is set to false when Events is destroyed,
         * allowing lambdas holding this context to safely bail out.
         */
        struct CallbackContext {
            Events *events;
            ResourceManager *resourceManager;
            std::atomic<bool> valid{true};
        };

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
        void RegisterHandler(v8::Isolate *isolate,
                            ResourceManager *manager,
                            const std::string &eventName,
                            v8::Local<v8::Function> handler,
                            bool once);

        // Internal emit helper - returns Promise
        v8::Local<v8::Promise> EmitInternal(v8::Isolate *isolate,
                                            v8::Local<v8::Context> context,
                                            const std::string &eventName,
                                            const std::vector<v8::Local<v8::Value>> &args,
                                            const std::string &targetResource = "");

        // Global handlers: eventName -> handlers (FIFO order)
        std::map<std::string, std::vector<EventHandler>> _globalHandlers;

        // Local handlers: resourceName -> eventName -> handlers
        std::map<std::string, std::map<std::string, std::vector<EventHandler>>> _localHandlers;

        std::mutex _handlersMutex;
        ResourceManager *_resourceManager = nullptr;

        // Stored callback context (lifetime tied to this Events instance)
        std::unique_ptr<CallbackContext> _callbackContext;
    };

} // namespace Framework::Scripting
