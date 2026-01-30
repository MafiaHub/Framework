#pragma once

#include <v8pp/convert.hpp>

#include <v8.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
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
         * Cleanup all static V8 globals.
         * Must be called before the V8 isolate is disposed.
         */
        static void Shutdown();

      private:
        // V8 callback implementations
        static void HandleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void RequestCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SendCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Message handler storage: resourceName -> messageType -> handler
        static std::map<std::string, std::map<std::string, v8::Global<v8::Function>>> _handlers;
        static std::mutex _handlersMutex;

        // Reply context for safe reply function handling - prevents double-free
        // and use-after-free when reply() is called multiple times or errors occur.
        struct ReplyContext {
            uint64_t id;
            std::atomic<bool> consumed{false};
            explicit ReplyContext(uint64_t requestId) : id(requestId) {}
        };

        // Pending request structure
        struct PendingRequest {
            uint64_t requestId;
            v8::Global<v8::Promise::Resolver> resolver;
            std::string sourceResource;
            std::shared_ptr<ReplyContext> replyContext; // Keeps ReplyContext alive
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
