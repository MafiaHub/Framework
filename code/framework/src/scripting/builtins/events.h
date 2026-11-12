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

#include "../bind/callable.h"
#include "../bind/namespace.h"
#include "../value_transfer.h"

#include <function2/function2.hpp>

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
    class LuaRuntime;
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {

    // The JavaScript function of a handler, shared by its table entry and by the emissions that picked it: emptied
    // when its isolate goes, whoever still holds it, so that no handle outlives the isolate it belongs to.
    using EventFunction = std::shared_ptr<v8::Global<v8::Function>>;

    /**
     * Handler entry with metadata
     */
    struct EventHandler {
        // The function, whatever its language: what tells two handlers apart, what a Lua emitter and a Lua handler
        // are invoked through, and what says that the runtime of the handler has stopped.
        std::shared_ptr<Bind::ScriptCallable> callable;

        // A handler registered from JavaScript, as JavaScript emitters call it: by reference in its own isolate,
        // through the engine's function references from another. Null for a Lua handler.
        EventFunction callback;
        std::string resourceName;
        bool once = false;

        // The isolate the callback lives in. A handler in another isolate than the emitter's is called through the
        // engine's function references, with the arguments copied across. nullptr for a Lua handler.
        v8::Isolate *isolate = nullptr;
    };

    // A handler picked for one emission, held outside the table lock while it runs.
    struct CollectedHandler {
        std::shared_ptr<Bind::ScriptCallable> callable;
        EventFunction callback;
        std::string resourceName;
        v8::Isolate *isolate = nullptr;

        explicit CollectedHandler(const EventHandler &handler): callable(handler.callable), callback(handler.callback), resourceName(handler.resourceName), isolate(handler.isolate) {}
    };

    /**
     * Events system - global event bus with async support, for JavaScript and Lua alike.
     *
     * One definition (Definition) declares the Events global of both languages:
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
     *
     * Dispatch across languages:
     * A JavaScript emitter calls a JavaScript handler as it always did: with the very arguments in its own isolate,
     * through the engine's function references in another. Whatever involves Lua, a Lua handler or a Lua emitter,
     * goes through the callable of the handler with the arguments copied once per emission, and the handler is
     * called for its outcome only: a veto, a failure, and when it is done. A JavaScript emitter gets the Promise
     * it always got. A Lua emit waits until every handler has settled, returns nothing, and raises the message
     * the Promise would be rejected with followed by what each handler failed with.
     */
    class Events final {
      public:
        Events() = default;
        ~Events();

        // Non-copyable
        Events(const Events &)            = delete;
        Events &operator=(const Events &) = delete;

        // What an emission made with copied arguments is told once every handler has settled: what each handler
        // that failed threw, in the order the handlers were called. Empty when none failed. It must not throw.
        using EmissionDone = fu2::unique_function<void(std::vector<std::string> &&errors)>;

        // An emission made with copied arguments that still waits on its handlers, for CancelPendingEmission. Empty
        // for one that had settled when it was made.
        struct CopiedEmission;
        using EmissionToken = std::shared_ptr<CopiedEmission>;

        // The Events global of both languages.
        static const Bind::Namespace &Definition();

        // Publishes Events in a Lua runtime. Its functions work on the Events of the resource manager the calls
        // of the runtime answer with (Bind::SetCallResourceManager).
        static void RegisterLua(LuaRuntime &runtime);

        /**
         * Register the Events object on the target. isClient exposes emitServer (client -> onClient);
         * otherwise exposes emitAllClients (server -> every client's on()).
         *
         * An isolate has one Events: the functions of the definition find theirs by the isolate they run in, so
         * the last Register of an isolate wins, also for the Events objects earlier calls made there.
         */
        void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager, bool isClient = false);

        /**
         * Emit an event from native code to all global handlers.
         * Returns a Promise that resolves when all handlers complete.
         */
        v8::Local<v8::Promise> EmitReserved(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args);

        /**
         * EmitReserved for a host that has no isolate to emit from, as a server without a JavaScript engine: the
         * arguments are copies, every handler of either language is called through its callable, and `done` is
         * told when they have all settled, which may be before this returns. A caller that stops waiting hands what
         * this returns to CancelPendingEmission.
         */
        EmissionToken EmitReserved(const std::string &eventName, std::vector<TransferredValue> args, EmissionDone done);

        // Lets go of the `done` of an emission made with copied arguments, which is then never told. Its handlers
        // run on. Nothing for an emission that has settled.
        static void CancelPendingEmission(const EmissionToken &emission);

        // What a failed emission is described with: `message`, then what each handler failed with.
        static std::string DescribeFailures(std::string_view message, const std::vector<std::string> &errors);

        /**
         * Synchronous EmitReserved. Every handler runs; returning literal false vetoes, and this
         * returns false when any did. A returned Promise is ignored — the caller needs its answer
         * before it returns, so async handlers cannot veto: neither can a Lua handler that waits.
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

        // ForgetIsolate on the Events the isolate was registered with, if any. Builtins::UnregisterAll calls it for
        // every isolate an engine disposes of, the engine's own included, which no runtime listener speaks for:
        // nothing here then outlives the isolate it holds handles of.
        static void UnregisterIsolate(v8::Isolate *isolate);

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
        // Selects which handler table an operation targets. The client table backs onClient()
        // and EmitClient(); it is deliberately disjoint from the global table (on()/emit()) so
        // client-originated event names cannot reach native handlers.
        enum class HandlerScope {
            Global,
            Client
        };

        // What the functions of the definition run. Defined in events.cpp.
        struct Script;

        // The arguments of an emission copied for the handlers that are called through their callable, made on the
        // first of them.
        struct CallableArguments;

        // Returns the handler table backing the given scope.
        std::map<std::string, std::vector<EventHandler>, std::less<>> &HandlerTable(HandlerScope scope);

        // Snapshot (callback, resourceName) pairs for an event, consuming once handlers, so
        // dispatch runs outside the table lock.
        std::vector<CollectedHandler> CollectHandlers(const std::string &eventName, const std::string &targetResource, HandlerScope scope);

        // Snapshot of the local handlers of a resource for an event.
        std::vector<CollectedHandler> CollectLocalHandlers(const std::string &resourceName, const std::string &eventName);

        // Internal emit helper - returns Promise
        v8::Local<v8::Promise> EmitInternal(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource, HandlerScope scope, std::string_view api);

        // Calls `handlers` from the isolate of the emitter and returns the Promise of the emission, rejected with an
        // AggregateError carrying `aggregateErrorMessage` when a handler fails. `api` names the emitting function
        // where an argument cannot be copied for a Lua handler.
        v8::Local<v8::Promise> EmitToHandlers(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<CollectedHandler> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName, const std::string &aggregateErrorMessage,
            std::string_view api);

        // Calls `handlers` for an emitter that has no isolate, a Lua script or a host without JavaScript, each
        // through its callable, and tells `done` when all have settled.
        static EmissionToken EmitCopied(std::vector<CollectedHandler> handlers, const std::vector<TransferredValue> &args, const std::string &eventName, EmissionDone done);

        // Helper: invoke handlers and collect results as promises in the emitter's isolate. A handler's resource name
        // is its log context.
        v8::Local<v8::Array> InvokeHandlersToPromiseArray(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<CollectedHandler> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName, std::string_view api);

        // Calls one JavaScript handler and returns what it returned, or empty with the exception caught by the caller's
        // TryCatch. `copied` holds the arguments copied out of the emitter, made on the first handler in another
        // isolate. A handler running past the resource monitor's threshold is reported under `eventName`.
        v8::MaybeLocal<v8::Value> InvokeHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, CollectedHandler &handler, const std::vector<v8::Local<v8::Value>> &args, std::optional<std::vector<TransferredValue>> &copied, std::string_view eventName);

        // Calls one Lua handler from the isolate of the emitter, through its callable, and returns a promise of that
        // isolate that settles as the handler does: at once when it finished at once, when it stops waiting
        // otherwise.
        v8::Local<v8::Value> InvokeCallable(v8::Isolate *isolate, v8::Local<v8::Context> context, CollectedHandler &handler, const std::vector<v8::Local<v8::Value>> &args, CallableArguments &copied, const std::string &eventName, std::string_view api);

        // Forward declaration for callback data tracking
        struct AllSettledCallbackData;

        // Helper: aggregate promises using Promise.allSettled and resolve/reject the resolver
        // Non-static to track pending callbacks for cleanup on destruction
        void AggregateWithAllSettled(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Array> promises, v8::Local<v8::Promise::Resolver> resolver, const std::string &aggregateErrorMessage);

        // Remove callback data from tracking (called when promise settles)
        // Takes raw pointer and finds the corresponding shared_ptr to remove
        void RemovePendingCallback(AllSettledCallbackData *data);
        void CancelPendingCallbacks();

        // Global handlers: eventName -> handlers (FIFO order)
        std::map<std::string, std::vector<EventHandler>, std::less<>> _globalHandlers;

        // Client-originated handlers (onClient), kept disjoint from _globalHandlers so a
        // client-supplied event name can never dispatch into a native/global listener.
        std::map<std::string, std::vector<EventHandler>, std::less<>> _clientHandlers;

        // Local handlers: resourceName -> eventName -> handlers
        std::map<std::string, std::map<std::string, std::vector<EventHandler>, std::less<>>, std::less<>> _localHandlers;

        // The functions of the handlers that emissions in progress picked, by isolate: a handler may leave its table
        // while an emission still holds it, and its isolate may go before that emission ends. Under _handlersMutex.
        std::map<v8::Isolate *, std::vector<std::weak_ptr<v8::Global<v8::Function>>>> _dispatching;

        // Notes what an emission picked. Call with _handlersMutex held.
        void TrackDispatch(const std::vector<CollectedHandler> &handlers);

        mutable std::mutex _handlersMutex;

        // The manager Register was last given: whose function references reach a handler in another isolate.
        ResourceManager *_resourceManager = nullptr;

        // What an unsubscribe function finds this Events through: it holds nullptr once the Events is gone, so a
        // function a script kept beyond it does nothing.
        std::shared_ptr<Events *> _self = std::make_shared<Events *>(this);

        // Track pending AllSettled callbacks for cleanup on destruction
        std::set<std::shared_ptr<AllSettledCallbackData>> _pendingCallbacks;
        std::mutex _pendingCallbacksMutex;

        // Emissions waiting for their handlers to settle, by the key their then-handler holds. Static, because the
        // then-handler can run after this Events is gone, and the disposal of a runtime must reach the emissions whose
        // then-handlers will now never run.
        static std::map<uint64_t, std::shared_ptr<AllSettledCallbackData>> _waitingEmissions;
        static std::mutex _waitingEmissionsMutex;
        static uint64_t _nextWaitingEmission;
    };

} // namespace Framework::Scripting::Builtins
