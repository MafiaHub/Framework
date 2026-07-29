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
#include <mutex>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::Scripting::Builtins {

    // Global Messages: request/response and fire-and-forget channel between local resources.
    //   handle(messageType, handler) / request(resource, type, payload) -> Promise / send(...)
    class Messages final {
      public:
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> target,
                            ResourceManager *resourceManager);

        /**
         * Process pending message responses (call in update loop).
         */
        static void ProcessPendingResponses(v8::Isolate *isolate, v8::Local<v8::Context> context);

        /**
         * Drop a stopped resource's registered handlers, and reject+erase every
         * pending request it originated or targeted, so its Global<Function>
         * handles don't dangle and awaiting `request(...)` Promises reject
         * instead of hanging until isolate teardown. Needs an active
         * isolate/context to settle those Promises. Mirrors Events::CleanupResource.
         */
        static void CleanupResource(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &resourceName);

        /**
         * Cleanup all static V8 globals.
         * Must be called before the V8 isolate is disposed.
         */
        static void Shutdown();

      private:
        // V8 callback implementations
        static void HandleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void RequestCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SendCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // A registered handler together with the isolate its Global<Function> is
        // bound to. Requests/sends must not Get() a handler from a foreign isolate.
        struct Handler {
            v8::Isolate *isolate = nullptr;
            v8::Global<v8::Function> function;
        };

        // Message handler storage: resourceName -> messageType -> handler
        static std::map<std::string, std::map<std::string, Handler>> _handlers;
        static std::mutex _handlersMutex;

        // Pending request structure
        struct PendingRequest {
            uint64_t requestId;
            v8::Global<v8::Promise::Resolver> resolver;
            std::string sourceResource; // resource awaiting the reply (owns the Promise)
            std::string targetResource; // resource whose handler must reply
            std::atomic<bool> consumed{false}; // Prevents double-reply

            PendingRequest(uint64_t id, v8::Global<v8::Promise::Resolver> res, std::string src, std::string tgt)
                : requestId(id), resolver(std::move(res)), sourceResource(std::move(src)), targetResource(std::move(tgt)), consumed(false) {}
            PendingRequest(PendingRequest&&) = delete;
            PendingRequest& operator=(PendingRequest&&) = delete;
        };

        // Pending response structure
        struct PendingResponse {
            uint64_t requestId;
            v8::Global<v8::Value> response;
            bool isError;
        };

        static std::map<uint64_t, PendingRequest> _pendingRequests;
        static std::mutex _pendingRequestsMutex;

        static std::vector<PendingResponse> _responseQueue;
        static std::mutex _responseQueueMutex;

        static uint64_t _nextRequestId;
    };

} // namespace Framework::Scripting::Builtins
