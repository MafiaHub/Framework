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

    /**
     * JavaScript messages system for request/response communication.
     * Provides Framework.messages API:
     * - handle(messageType, handler) - Register handler
     * - request(resourceName, messageType, payload) - Returns Promise
     * - send(resourceName, messageType, payload) - Fire and forget
     */
    class Messages final {
      public:
        /**
         * Register the Framework.messages object in a context.
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            ResourceManager *resourceManager);

        /**
         * Process pending message responses (call in update loop).
         */
        static void ProcessPendingResponses(v8::Isolate *isolate, v8::Local<v8::Context> context);

        /**
         * Drop a stopped resource's registered handlers and any requests it
         * originated, so its Global<Function> handles don't dangle until the
         * whole isolate is torn down. Mirrors Events::CleanupResource.
         */
        static void CleanupResource(const std::string &resourceName);

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
            std::string sourceResource;
            std::atomic<bool> consumed{false}; // Prevents double-reply

            PendingRequest(uint64_t id, v8::Global<v8::Promise::Resolver> res, std::string src)
                : requestId(id), resolver(std::move(res)), sourceResource(std::move(src)), consumed(false) {}
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

        static ResourceManager *_resourceManager;
    };

} // namespace Framework::Scripting
