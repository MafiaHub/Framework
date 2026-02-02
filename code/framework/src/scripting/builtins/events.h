#pragma once

#include "events_reserved.h"

#include <v8pp/convert.hpp>
#include <v8.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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
    class Events final {
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

        /**
         * Add additional reserved events (e.g., from game-specific code).
         */
        void AddReservedEvents(const std::set<std::string> &events);

        /**
         * Check if an event name is reserved (static + dynamic).
         */
        bool IsEventReserved(const std::string &eventName) const;

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

        // Helper: invoke handlers and collect results as promises
        // handlers: pairs of (callback, logContext) where logContext is used for error logging
        static v8::Local<v8::Array> InvokeHandlersToPromiseArray(
            v8::Isolate *isolate,
            v8::Local<v8::Context> context,
            std::vector<std::pair<v8::Global<v8::Function>, std::string>> &handlers,
            const std::vector<v8::Local<v8::Value>> &args,
            const std::string &eventName);

        // Forward declaration for callback data tracking
        struct AllSettledCallbackData;

        // Helper: aggregate promises using Promise.allSettled and resolve/reject the resolver
        // Non-static to track pending callbacks for cleanup on destruction
        void AggregateWithAllSettled(
            v8::Isolate *isolate,
            v8::Local<v8::Context> context,
            v8::Local<v8::Array> promises,
            v8::Local<v8::Promise::Resolver> resolver,
            const std::string &aggregateErrorMessage);

        // Remove callback data from tracking (called when promise settles)
        // Takes raw pointer and finds the corresponding shared_ptr to remove
        void RemovePendingCallback(AllSettledCallbackData *data);

        // Global handlers: eventName -> handlers (FIFO order)
        std::map<std::string, std::vector<EventHandler>> _globalHandlers;

        // Local handlers: resourceName -> eventName -> handlers
        std::map<std::string, std::map<std::string, std::vector<EventHandler>>> _localHandlers;

        mutable std::mutex _handlersMutex;
        ResourceManager *_resourceManager = nullptr;

        // Additional reserved events added at runtime
        std::set<std::string> _additionalReservedEvents;

        // Stored callback context (lifetime tied to this Events instance)
        std::unique_ptr<CallbackContext> _callbackContext;

        // Track pending AllSettled callbacks for cleanup on destruction
        std::set<std::shared_ptr<AllSettledCallbackData>> _pendingCallbacks;
        std::mutex _pendingCallbacksMutex;
    };

} // namespace Framework::Scripting
