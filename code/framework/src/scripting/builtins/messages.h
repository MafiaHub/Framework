/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/callable.h"
#include "../bind/namespace.h"
#include "../transferred_value.h"

#include <v8pp/convert.hpp>

#include <v8.h>

#include <function2/function2.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {
    class LuaRuntime;
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {

    // Global Messages: request/response and fire-and-forget channel between local resources, declared once for
    // JavaScript and Lua.
    //   handle(messageType, handler) / request(resource, type, payload) -> Promise / send(...)
    //
    // Between two JavaScript resources nothing changed: a handler of the requester's isolate gets the payload and
    // the reply function themselves, one of another isolate a copy and a function reference. Where the requester or
    // the handler is Lua, the handler is called through its callable with a copy of the payload, and `reply` is a
    // function of the host that takes a copy of the answer.
    //
    // A request from Lua waits where a JavaScript one gets a Promise: it returns the reply, or raises what the
    // Promise would be rejected with. Either way the reply is delivered by ProcessPendingResponses, at the next
    // update, also when the handler answered at once. A request nobody answers waits until the resource that owes
    // the answer, or the one that asked, stops.
    class Messages final {
      public:
        static const Bind::Namespace &Definition();

        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager);

        static void RegisterLua(LuaRuntime &runtime);

        /**
         * Process pending message responses (call in update loop).
         */
        static void ProcessPendingResponses(v8::Isolate *isolate, v8::Local<v8::Context> context);

        /**
         * The same, for a host with no isolate at hand: every reply is delivered in the runtime of its requester,
         * which is entered for it when it is a JavaScript one.
         */
        static void ProcessPendingResponses();

        /**
         * Drop a stopped resource's registered handlers, and reject+erase every
         * pending request it originated or targeted, so its Global<Function>
         * handles don't dangle and awaiting `request(...)` Promises reject
         * instead of hanging until isolate teardown. Needs an active
         * isolate/context to settle those Promises. Mirrors Events::CleanupResource.
         */
        static void CleanupResource(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &resourceName);

        /**
         * The same, for a host with no isolate at hand, as a manager without a JavaScript engine is.
         */
        static void CleanupResource(const std::string &resourceName);

        /**
         * Drop every handle that lives in an isolate about to be disposed: its handlers, the requests it is waiting on
         * and the replies queued for them. Call while the isolate is alive.
         */
        static void ForgetIsolate(v8::Isolate *isolate);

        /**
         * Drop the handlers registered under a manager that is going, and report to nobody what a handler of
         * its resources still fails with. The manager calls it as it is destroyed.
         */
        static void ForgetManager(const ResourceManager *manager);

        /**
         * Drop the requests made from Lua that nobody waits for any more: those of a runtime that has stopped,
         * and those of a call that could not wait. Call it when a Lua runtime goes, after its callables were told
         * (Bind::WatchRuntimes); ProcessPendingResponses does it for the requests made since it last ran.
         */
        static void DropAbandonedRequests();

        /**
         * Cleanup all static V8 globals.
         * Must be called before the V8 isolate is disposed.
         */
        static void Shutdown();

        /**
         * Diagnostics and tests: the requests that still wait for a reply.
         */
        static size_t GetPendingRequestCount();

      private:
        // The functions of the definition.
        struct Script;

        // What a handler is given as `reply` when it is called through its callable.
        class ReplyFunction;

        // The manager handlers were registered under, until it goes: what a failing handler is reported to.
        struct ManagerLink {
            ResourceManager *manager = nullptr;
        };

        // A registered handler, and the manager of the resource that owns it. One registered from JavaScript also
        // has the isolate its Global<Function> is bound to, for a JavaScript requester. Requests/sends must not
        // Get() a handler from a foreign isolate.
        struct Handler {
            v8::Isolate *isolate = nullptr;
            v8::Global<v8::Function> function;
            std::shared_ptr<Bind::ScriptCallable> callable;
            std::shared_ptr<ManagerLink> manager;
        };

        // The link of `manager`, shared by every handler registered under it. Never null.
        static std::shared_ptr<ManagerLink> LinkOf(ResourceManager *manager);

        // By manager; under _handlersMutex.
        static std::map<const ResourceManager *, std::shared_ptr<ManagerLink>> _managers;

        // Message handler storage: resourceName -> messageType -> handler
        static std::map<std::string, std::map<std::string, Handler>> _handlers;
        static std::mutex _handlersMutex;

        // Pending request structure
        struct PendingRequest {
            uint64_t requestId;
            v8::Global<v8::Promise::Resolver> resolver;
            std::string sourceResource;         // resource awaiting the reply (owns the Promise)
            std::string targetResource;         // resource whose handler must reply
            std::atomic<bool> consumed {false}; // Prevents double-reply

            // Where the Promise lives: the requester's runtime, which may not be the one settling it. No isolate
            // for a request made from Lua.
            v8::Isolate *isolate = nullptr;
            v8::Global<v8::Context> context;

            // The wait of a request made from Lua.
            Bind::Deferred waiter;

            PendingRequest(uint64_t id, v8::Global<v8::Promise::Resolver> res, std::string src, std::string tgt, v8::Isolate *iso, v8::Local<v8::Context> ctx)
                : requestId(id)
                , resolver(std::move(res))
                , sourceResource(std::move(src))
                , targetResource(std::move(tgt))
                , consumed(false)
                , isolate(iso)
                , context(iso, ctx) {}
            PendingRequest(uint64_t id, Bind::Deferred wait, std::string src, std::string tgt): requestId(id), sourceResource(std::move(src)), targetResource(std::move(tgt)), consumed(false), waiter(std::move(wait)) {}
            PendingRequest(PendingRequest &&)            = delete;
            PendingRequest &operator=(PendingRequest &&) = delete;
        };

        using RequestNode = std::map<uint64_t, PendingRequest>::node_type;

        // Pending response structure
        struct PendingResponse {
            uint64_t requestId;
            v8::Global<v8::Value> response;
            bool isError;
            v8::Isolate *isolate = nullptr; // Where the response lives. None for one that is a copy.
            TransferredValue copy;          // The response of a handler that was called through its callable.
        };

        enum class Lookup : uint8_t {
            NoResource,
            NoHandler,
            Found
        };

        // The handler of `messageType` in `targetResource`, held by value. For a JavaScript requester
        // (`javaScript`) with its function, in the isolate it belongs to: the handler may live in another
        // resource's runtime. A handler that would be called through its callable and whose runtime is gone is
        // dropped, and not found.
        static Lookup FindHandler(const std::string &targetResource, const std::string &messageType, bool javaScript, Handler &handler);

        // Calls a handler in whichever runtime it lives in with (payload, reply). From another runtime the payload is
        // copied and reply crosses as a reference. Empty with an exception pending in the caller when it threw. A
        // handler running past the resource monitor's threshold is reported under `messageType`.
        static v8::MaybeLocal<v8::Value> CallHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, ResourceManager *manager, const Handler &handler, v8::Local<v8::Value> payload, v8::Local<v8::Function> reply, std::string_view messageType);

        // Calls a handler through its callable with (payload, reply), where reply answers request `requestId`, or
        // nothing for 0, a notification. Gives what the handler failed with before this returned, for the caller
        // to act on where it stands; a handler that fails later is dealt with by HandlerFailed.
        static std::optional<std::string> Dispatch(const Handler &handler, const std::string &targetResource, const std::string &messageType, uint64_t requestId, const std::string &sourceResource, TransferredValue payload);

        // Queues the answer to a request that has none yet.
        static void QueueReply(uint64_t requestId, TransferredValue response);

        // The request, out of the table, for whoever fails it. Empty when it was answered or is gone.
        static RequestNode TakeUnanswered(uint64_t requestId);

        // A handler that was called through its callable failed with `error`: fails the request that still waits
        // for it, and reports a failure nobody waits for to the manager of the handler, `manager`.
        static void HandlerFailed(const std::shared_ptr<ManagerLink> &manager, const std::string &targetResource, const std::string &messageType, uint64_t requestId, const std::string &error);

        // Reports what a handler failed with when no request waits for it, to the manager of the handler if it is
        // still there.
        static void ReportUnawaited(const std::shared_ptr<ManagerLink> &manager, const std::string &targetResource, const std::string &messageType, const std::string &error);

        // Fails a request with `message`: a JavaScript Promise is rejected with it, as an Error when `asError`
        // and as the string otherwise, and a Lua wait raises it.
        static void Reject(PendingRequest &request, const std::string &message, bool asError);

        // Settles a request's Promise inside the runtime it belongs to.
        static void Settle(PendingRequest &request, fu2::function_view<void(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Promise::Resolver>)> settle);

        static std::map<uint64_t, PendingRequest> _pendingRequests;
        static std::mutex _pendingRequestsMutex;

        static std::vector<PendingResponse> _responseQueue;
        static std::mutex _responseQueueMutex;

        static uint64_t _nextRequestId;

        // Whether a request was made from Lua since the abandoned ones were last dropped; under
        // _pendingRequestsMutex.
        static bool _luaRequestsUnchecked;
    };

} // namespace Framework::Scripting::Builtins
