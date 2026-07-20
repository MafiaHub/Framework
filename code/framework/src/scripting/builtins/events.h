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

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
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
     * - onClient(eventName, handler) - Listen for client-originated events (see below)
     * - onceClient / offClient - One-time / remove variants
     *
     * Client-event trust boundary:
     * onClient() handlers live in a table entirely separate from on()/emit(). Events arriving
     * from a client (EmitClient, native-only) are dispatched into that table and nothing else,
     * so a client-supplied event name can never resolve to a handler registered with on() — the
     * "server said this" vs "a client claims this" split is enforced by construction, not by a
     * naming convention. emit()/emitTo() from scripts likewise cannot reach the client table.
     */
    class Events final {
      public:
        Events() = default;
        ~Events();

        // Non-copyable
        Events(const Events &) = delete;
        Events &operator=(const Events &) = delete;

        /**
         * Register the Events object on the target. isClient exposes emitServer (client -> onClient);
         * otherwise exposes emitAllClients (server -> every client's on()).
         */
        void Register(v8::Isolate *isolate,
                     v8::Local<v8::Context> context,
                     v8::Local<v8::Object> target,
                     ResourceManager *resourceManager,
                     bool isClient = false);

        /**
         * Emit an event from native code to all global handlers.
         * Returns a Promise that resolves when all handlers complete.
         */
        v8::Local<v8::Promise> EmitReserved(v8::Isolate *isolate,
                                            v8::Local<v8::Context> context,
                                            const std::string &eventName,
                                            const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Emit a client-originated event from native code. Dispatched ONLY to onClient()
         * handlers, which live in a table separate from the native/global bus — so a
         * client-supplied event name can never resolve to an on() handler. Returns a Promise
         * that resolves when all handlers complete.
         */
        v8::Local<v8::Promise> EmitClient(v8::Isolate *isolate,
                                          v8::Local<v8::Context> context,
                                          const std::string &eventName,
                                          const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Clean up all handlers for a resource (called on resource stop).
         */
        void CleanupResource(std::string_view resourceName);

        /**
         * Clear all handlers (called on shutdown).
         */
        void ClearAll();

        /**
         * Get count of handlers for an event (global/on() table).
         */
        size_t GetListenerCount(std::string_view eventName);

        /**
         * Get count of client-event handlers for an event (onClient() table). Disjoint from
         * GetListenerCount, mirroring the on()/onClient() table split.
         */
        size_t GetClientListenerCount(std::string_view eventName);

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

        // Selects which handler table an operation targets. The client table backs onClient()
        // and EmitClient(); it is deliberately disjoint from the global table (on()/emit()) so
        // client-originated event names cannot reach native handlers.
        enum class HandlerScope { Global, Client };

        // V8 callbacks. on/once/off are shared by the global and client scopes via the *Impl
        // helpers; the bound callbacks only pin the scope.
        static void OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnceCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnClientCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnceClientCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OffClientCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        // Script-level client<->server event bridge: emitServer (client), emitAllClients (server).
        static void EmitServerCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitAllClientsCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitToCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OnLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void EmitLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ListenerCountCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Scope-aware bodies behind the on/once/off callbacks.
        static void OnImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope);
        static void OnceImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope);
        static void OffImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope);

        // Returns the handler table backing the given scope.
        std::map<std::string, std::vector<EventHandler>, std::less<>> &HandlerTable(HandlerScope scope);

        // Internal registration helper
        void RegisterHandler(v8::Isolate *isolate,
                            ResourceManager *manager,
                            std::string_view eventName,
                            v8::Local<v8::Function> handler,
                            bool once,
                            HandlerScope scope);

        // Internal emit helper - returns Promise
        v8::Local<v8::Promise> EmitInternal(v8::Isolate *isolate,
                                            v8::Local<v8::Context> context,
                                            const std::string &eventName,
                                            const std::vector<v8::Local<v8::Value>> &args,
                                            const std::string &targetResource = "",
                                            HandlerScope scope = HandlerScope::Global);

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
        std::map<std::string, std::vector<EventHandler>, std::less<>> _globalHandlers;

        // Client-originated handlers (onClient), kept disjoint from _globalHandlers so a
        // client-supplied event name can never dispatch into a native/global listener.
        std::map<std::string, std::vector<EventHandler>, std::less<>> _clientHandlers;

        // Local handlers: resourceName -> eventName -> handlers
        std::map<std::string, std::map<std::string, std::vector<EventHandler>, std::less<>>, std::less<>> _localHandlers;

        mutable std::mutex _handlersMutex;

        // Callback context passed to V8 via Externals. Allocated once and reused, never
        // reallocated — its address outlives individual Register() calls. See Events::Register.
        std::unique_ptr<CallbackContext> _callbackContext;

        // Track pending AllSettled callbacks for cleanup on destruction
        std::set<std::shared_ptr<AllSettledCallbackData>> _pendingCallbacks;
        std::mutex _pendingCallbacksMutex;
    };

} // namespace Framework::Scripting
