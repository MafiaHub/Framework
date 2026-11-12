/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/convert.hpp>

#include "../value_transfer.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::Scripting::Builtins {

    /**
     * Handler entry with metadata
     */
    struct EventHandler {
        v8::Global<v8::Function> callback;
        std::string resourceName;
        bool once;

        // The isolate the callback lives in. A handler in another isolate than the emitter's is called through the
        // engine's function references, with the arguments copied across.
        v8::Isolate *isolate = nullptr;
    };

    // A handler picked for one emission, held outside the table lock while it runs.
    struct CollectedHandler {
        v8::Global<v8::Function> callback;
        std::string resourceName;
        v8::Isolate *isolate = nullptr;

        explicit CollectedHandler(const EventHandler &handler): callback(handler.isolate, handler.callback), resourceName(handler.resourceName), isolate(handler.isolate) {}
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
        Events(const Events &)            = delete;
        Events &operator=(const Events &) = delete;

        /**
         * Register the Events object on the target. isClient exposes emitServer (client -> onClient);
         * otherwise exposes emitAllClients (server -> every client's on()).
         */
        void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager, bool isClient = false);

        /**
         * Emit an event from native code to all global handlers.
         * Returns a Promise that resolves when all handlers complete.
         */
        v8::Local<v8::Promise> EmitReserved(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Synchronous EmitReserved. Every handler runs; returning literal false vetoes, and this
         * returns false when any did. A returned Promise is ignored — the caller needs its answer
         * before it returns, so async handlers cannot veto.
         */
        bool EmitReservedSync(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Emit a native event to the global handlers of one resource only, for events the
         * framework can attribute to an owner. Returns a Promise that resolves when all
         * handlers complete.
         */
        v8::Local<v8::Promise> EmitReservedTo(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &resourceName);

        /**
         * Emit a client-originated event from native code. Dispatched ONLY to onClient()
         * handlers, which live in a table separate from the native/global bus — so a
         * client-supplied event name can never resolve to an on() handler. Returns a Promise
         * that resolves when all handlers complete.
         */
        v8::Local<v8::Promise> EmitClient(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args);

        /**
         * Release the native resolver retained for one emitted Promise. Used by bounded native
         * callers when they stop waiting; unrelated event emissions remain intact.
         */
        void CancelPendingEmission(v8::Isolate *isolate, v8::Local<v8::Promise> promise);

        /**
         * Clean up all handlers for a resource (called on resource stop).
         */
        void CleanupResource(std::string_view resourceName);

        /**
         * Drop every handle that lives in an isolate about to be disposed: handlers, and the resolvers of emissions
         * still waiting on their handlers. Call while the isolate is alive.
         */
        void ForgetIsolate(v8::Isolate *isolate);

        // Emissions still waiting for their handlers to settle, across every Events. For diagnostics and tests.
        static size_t GetWaitingEmissionCount();

        /**
         * Clear all per-session event state.
         */
        void Reset();

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
            std::atomic<bool> valid {true};
        };

        // Selects which handler table an operation targets. The client table backs onClient()
        // and EmitClient(); it is deliberately disjoint from the global table (on()/emit()) so
        // client-originated event names cannot reach native handlers.
        enum class HandlerScope {
            Global,
            Client
        };

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

        // Snapshot (callback, resourceName) pairs for an event, consuming once handlers, so
        // dispatch runs outside the table lock.
        std::vector<CollectedHandler> CollectHandlers(const std::string &eventName, const std::string &targetResource, HandlerScope scope);

        // Internal registration helper
        void RegisterHandler(v8::Isolate *isolate, ResourceManager *manager, std::string_view eventName, v8::Local<v8::Function> handler, bool once, HandlerScope scope);

        // Internal emit helper - returns Promise
        v8::Local<v8::Promise> EmitInternal(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource = "", HandlerScope scope = HandlerScope::Global);
        // An emit from a thread that is not the script thread (a server's tick thread raising an event from an RPC
        // handler). Handlers never run there: the arguments are copied out of the caller's isolate and the emit
        // itself runs on the script thread. `sync` waits for it and returns whether no handler vetoed, and false when the
        // event could not be raised at all; without it the call returns at once (true) and nothing of the handlers
        // comes back.
        bool EmitFromOtherThread(v8::Isolate *isolate, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource, HandlerScope scope, bool sync);

        // Calls every handler and settles `resolver` once they all have: rejected with an AggregateError of the failures
        // in handler order, else resolved. Each failure is logged under its handler's resource as it happens.
        void DispatchHandlers(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<CollectedHandler> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName, v8::Local<v8::Promise::Resolver> resolver, const char *aggregateErrorMessage);

        // Calls one handler and returns what it returned, or empty with the exception caught by the caller's TryCatch.
        // `copied` holds the arguments copied out of the emitter, made on the first handler in another isolate. A
        // handler running past the resource monitor's threshold is reported under `eventName`.
        v8::MaybeLocal<v8::Value> InvokeHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, CollectedHandler &handler, const std::vector<v8::Local<v8::Value>> &args, std::optional<std::vector<TransferredValue>> &copied, std::string_view eventName);

        // An emission's handlers that have not settled yet, and what they failed with so far.
        struct PendingEmission;

        // Waits on the promise handler `handler` returned; `key` is the emission's, assigned on its first promise.
        void WatchHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<PendingEmission> &emission, uint64_t &key, size_t handler, const std::string &resourceName, const std::string &eventName, v8::Local<v8::Promise> promise);
        static void OnHandlerFulfilled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void OnHandlerRejected(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void OnHandlerSettled(const v8::FunctionCallbackInfo<v8::Value> &info, bool rejected);
        // Counts one handler (or the dispatch itself) settled, and settles the emission after the last one.
        static void ReleaseEmission(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<PendingEmission> &emission, uint64_t key);
        static void LogHandlerRejection(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &resourceName, const std::string &eventName, v8::Local<v8::Value> reason);

        void RemovePendingCallback(PendingEmission *data);
        void CancelPendingCallbacks();

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

        // Emissions waiting on their handlers, cancelled on destruction
        std::set<std::shared_ptr<PendingEmission>> _pendingCallbacks;
        std::mutex _pendingCallbacksMutex;

        // Emissions waiting for their handlers to settle, by the key their handlers' reactions hold. Static, because a
        // reaction can run after this Events is gone, and the disposal of a runtime must reach the emissions whose
        // reactions will now never run.
        static std::map<uint64_t, std::shared_ptr<PendingEmission>> _waitingEmissions;
        static std::mutex _waitingEmissionsMutex;
        static uint64_t _nextWaitingEmission;
    };

} // namespace Framework::Scripting::Builtins
