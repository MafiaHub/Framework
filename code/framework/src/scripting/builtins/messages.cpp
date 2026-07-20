/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "messages.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"

#include <logging/logger.h>

namespace Framework::Scripting::Builtins {

    std::map<std::string, std::map<std::string, Messages::Handler>> Messages::_handlers;
    std::mutex Messages::_handlersMutex;
    std::map<uint64_t, Messages::PendingRequest> Messages::_pendingRequests;
    std::mutex Messages::_pendingRequestsMutex;
    std::vector<Messages::PendingResponse> Messages::_responseQueue;
    std::mutex Messages::_responseQueueMutex;
    uint64_t Messages::_nextRequestId = 1;

    void Messages::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> frameworkObj, ResourceManager *resourceManager) {
        v8::Local<v8::Object> messagesObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // handle(messageType, handler)
        v8::Local<v8::FunctionTemplate> handleTmpl = v8::FunctionTemplate::New(isolate, HandleCallback, managerData);
        messagesObj->Set(context, v8pp::to_v8(isolate, "handle"), handleTmpl->GetFunction(context).ToLocalChecked()).Check();

        // request(resourceName, messageType, payload)
        v8::Local<v8::FunctionTemplate> requestTmpl = v8::FunctionTemplate::New(isolate, RequestCallback, managerData);
        messagesObj->Set(context, v8pp::to_v8(isolate, "request"), requestTmpl->GetFunction(context).ToLocalChecked()).Check();

        // send(resourceName, messageType, payload)
        v8::Local<v8::FunctionTemplate> sendTmpl = v8::FunctionTemplate::New(isolate, SendCallback, managerData);
        messagesObj->Set(context, v8pp::to_v8(isolate, "send"), sendTmpl->GetFunction(context).ToLocalChecked()).Check();

        frameworkObj->Set(context, v8pp::to_v8(isolate, "messages"), messagesObj).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("messages", "Typed request and notification channel between local resources through Framework.messages.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("handle",
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("messageType", "string", false, "Message type unique within the receiving resource."),
                    v8pp::metadata::param("handler", "(payload: unknown, reply: (value: unknown) => void) => void", false, "Handler invoked with the payload and a reply callback; the reply is ignored for notifications."),
                },
                "Registers or replaces a message handler owned by the calling resource.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("request", v8pp::metadata::docs("Promise<unknown>",
                                                                                         {
                                                                                             v8pp::metadata::param("resourceName", "string", false, "Destination running resource."),
                                                                                             v8pp::metadata::param("messageType", "string", false, "Handler type registered by the destination."),
                                                                                             v8pp::metadata::param("payload", "unknown", true, "Optional payload delivered to the handler."),
                                                                                         },
                                                                                         "Sends a request to another local resource and waits for its handler to call reply.", "Promise resolved with the reply value or rejected when delivery or handling fails.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("send", v8pp::metadata::docs("void",
                                                                                      {
                                                                                          v8pp::metadata::param("resourceName", "string", false, "Destination running resource."),
                                                                                          v8pp::metadata::param("messageType", "string", false, "Handler type registered by the destination."),
                                                                                          v8pp::metadata::param("payload", "unknown", true, "Optional payload delivered to the handler."),
                                                                                      },
                                                                                      "Sends a fire-and-forget notification to another local resource.")));
    }

    void Messages::HandleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.handle requires 2 arguments: messageType, handler")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.handle: messageType must be a string")));
            return;
        }

        if (!args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.handle: handler must be a function")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle: resource manager not available")));
            return;
        }

        std::string messageType  = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string resourceName = manager->GetCurrentResourceContext();

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle: must be called from within a resource")));
            return;
        }

        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        {
            std::scoped_lock lock(_handlersMutex);
            Handler &entry = _handlers[resourceName][messageType];
            entry.isolate  = isolate;
            entry.function.Reset(isolate, handler);
        }
    }

    void Messages::RequestCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.request requires at least 2 arguments: resourceName, messageType")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.request: resourceName must be a string")));
            return;
        }

        if (!args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.request: messageType must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.request: resource manager not available")));
            return;
        }

        std::string targetResource = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string messageType    = v8pp::from_v8<std::string>(isolate, args[1]);
        std::string sourceResource = manager->GetCurrentResourceContext();

        v8::Local<v8::Value> payload = args.Length() > 2 ? args[2] : v8::Undefined(isolate).As<v8::Value>();

        // Create a Promise
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise            = resolver->GetPromise();

        // Check if target resource has a handler
        v8::Global<v8::Function> handler;
        {
            std::scoped_lock lock(_handlersMutex);
            auto resourceIt = _handlers.find(targetResource);
            if (resourceIt == _handlers.end()) {
                resolver->Reject(context, v8pp::to_v8(isolate, "Target resource not found")).Check();
                args.GetReturnValue().Set(promise);
                return;
            }

            auto handlerIt = resourceIt->second.find(messageType);
            if (handlerIt == resourceIt->second.end()) {
                resolver->Reject(context, v8pp::to_v8(isolate, "No handler for message type")).Check();
                args.GetReturnValue().Set(promise);
                return;
            }

            // The handler's Global<Function> is bound to the isolate it was registered on.
            // Getting it from a different isolate crashes, so reject cross-isolate requests
            // (mirrors the Exports.get isolate-ownership guard).
            if (handlerIt->second.isolate != isolate) {
                resolver->Reject(context, v8pp::to_v8(isolate, "messages.request: cannot invoke handler '" + messageType + "' in resource '" + targetResource + "' - cross-isolate access is not supported. Both resources must share the same isolate.")).Check();
                args.GetReturnValue().Set(promise);
                return;
            }

            handler.Reset(isolate, handlerIt->second.function.Get(isolate));
        }

        // Generate request ID and store pending request
        uint64_t requestId;
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            requestId = _nextRequestId++;
            _pendingRequests.try_emplace(requestId, requestId, v8::Global<v8::Promise::Resolver>(isolate, resolver), sourceResource);
        }

        // Create reply function - passes requestId as BigInt to avoid dangling pointer issues
        auto replyCallback = [](const v8::FunctionCallbackInfo<v8::Value> &replyArgs) {
            v8::Isolate *replyIsolate = replyArgs.GetIsolate();
            v8::HandleScope replyScope(replyIsolate);

            // Extract requestId from BigInt
            v8::Local<v8::BigInt> bigInt = replyArgs.Data().As<v8::BigInt>();
            uint64_t reqId               = bigInt->Uint64Value();

            // Look up PendingRequest and atomically check-and-set consumed flag
            {
                std::scoped_lock lock(_pendingRequestsMutex);
                auto it = _pendingRequests.find(reqId);
                if (it == _pendingRequests.end()) {
                    return; // Request no longer exists
                }

                // Atomically check-and-set consumed flag - only the first call proceeds
                if (it->second.consumed.exchange(true)) {
                    return; // Already consumed, ignore subsequent calls
                }
            }

            v8::Local<v8::Value> response = replyArgs.Length() > 0 ? replyArgs[0] : v8::Undefined(replyIsolate).As<v8::Value>();

            {
                std::scoped_lock lock(_responseQueueMutex);
                PendingResponse pendingResponse;
                pendingResponse.requestId = reqId;
                pendingResponse.response.Reset(replyIsolate, response);
                pendingResponse.isError = false;
                _responseQueue.push_back(std::move(pendingResponse));
            }
        };

        v8::Local<v8::BigInt> requestIdData = v8::BigInt::NewFromUnsigned(isolate, requestId);
        v8::Local<v8::Function> replyFn     = v8::Function::New(context, replyCallback, requestIdData).ToLocalChecked();

        // Call the handler with (payload, reply)
        v8::Local<v8::Function> handlerFn = handler.Get(isolate);
        v8::Local<v8::Value> argv[2]      = {payload, replyFn};

        // Save current resource context and set to target resource
        std::string previousContext = manager->GetCurrentResourceContext();
        manager->SetCurrentResourceContext(targetResource);

        v8::TryCatch tryCatch(isolate);
        v8::MaybeLocal<v8::Value> result = handlerFn->Call(context, context->Global(), 2, argv);

        if (tryCatch.HasCaught()) {
            v8::String::Utf8Value error(isolate, tryCatch.Exception());
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Message handler '{}' error: {}", targetResource, messageType, *error ? *error : "Unknown error");

            // Only reject the promise if reply() wasn't already called before the error
            std::scoped_lock lock(_pendingRequestsMutex);
            auto it = _pendingRequests.find(requestId);
            if (it != _pendingRequests.end() && !it->second.consumed.exchange(true)) {
                it->second.resolver.Get(isolate)->Reject(context, tryCatch.Exception()).Check();
                _pendingRequests.erase(it);
            }
        }

        // Restore previous resource context
        manager->SetCurrentResourceContext(previousContext);

        args.GetReturnValue().Set(promise);
    }

    void Messages::SendCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.send requires at least 2 arguments: resourceName, messageType")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.send: resourceName must be a string")));
            return;
        }

        if (!args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "messages.send: messageType must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());

        std::string targetResource = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string messageType    = v8pp::from_v8<std::string>(isolate, args[1]);

        v8::Local<v8::Value> payload = args.Length() > 2 ? args[2] : v8::Undefined(isolate).As<v8::Value>();

        // Find handler
        v8::Global<v8::Function> handler;
        {
            std::scoped_lock lock(_handlersMutex);
            auto resourceIt = _handlers.find(targetResource);
            if (resourceIt == _handlers.end()) {
                return; // Silently ignore
            }

            auto handlerIt = resourceIt->second.find(messageType);
            if (handlerIt == resourceIt->second.end()) {
                return; // Silently ignore
            }

            // Cross-isolate handler: Getting its Global<Function> from this isolate would
            // crash. Skip delivery (send is fire-and-forget) and warn (mirrors Exports.get).
            if (handlerIt->second.isolate != isolate) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("messages.send to '{}' skipped: handler '{}' lives in a different isolate", targetResource, messageType);
                return;
            }

            handler.Reset(isolate, handlerIt->second.function.Get(isolate));
        }

        // Create no-op reply function
        auto noOpReply                  = [](const v8::FunctionCallbackInfo<v8::Value> &) {};
        v8::Local<v8::Function> replyFn = v8::Function::New(context, noOpReply).ToLocalChecked();

        // Call the handler with (payload, reply)
        v8::Local<v8::Function> handlerFn = handler.Get(isolate);
        v8::Local<v8::Value> argv[2]      = {payload, replyFn};

        // Save current resource context and set to target resource
        std::string previousContext = manager ? manager->GetCurrentResourceContext() : "";
        if (manager) {
            manager->SetCurrentResourceContext(targetResource);
        }

        v8::TryCatch tryCatch(isolate);
        handlerFn->Call(context, context->Global(), 2, argv);

        if (tryCatch.HasCaught()) {
            v8::String::Utf8Value error(isolate, tryCatch.Exception());
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Message handler '{}' error: {}", targetResource, messageType, *error ? *error : "Unknown error");
        }

        // Restore previous resource context
        if (manager) {
            manager->SetCurrentResourceContext(previousContext);
        }
    }

    void Messages::ProcessPendingResponses(v8::Isolate *isolate, v8::Local<v8::Context> context) {
        std::vector<PendingResponse> responses;
        {
            std::scoped_lock lock(_responseQueueMutex);
            responses = std::move(_responseQueue);
            _responseQueue.clear();
        }

        for (auto &response : responses) {
            std::scoped_lock lock(_pendingRequestsMutex);
            auto it = _pendingRequests.find(response.requestId);
            if (it == _pendingRequests.end()) {
                continue;
            }

            v8::Local<v8::Promise::Resolver> resolver = it->second.resolver.Get(isolate);
            v8::Local<v8::Value> responseValue        = response.response.Get(isolate);

            if (response.isError) {
                resolver->Reject(context, responseValue).Check();
            }
            else {
                resolver->Resolve(context, responseValue).Check();
            }

            _pendingRequests.erase(it);
        }
    }

    void Messages::CleanupResource(const std::string &resourceName) {
        // Drop the stopped resource's registered handlers so its Global<Function>
        // handles are released now instead of lingering until Shutdown().
        {
            std::scoped_lock lock(_handlersMutex);
            _handlers.erase(resourceName);
        }

        // Drop any requests this resource originated; their resolver Globals would
        // otherwise dangle (the reply can never be delivered to a stopped resource).
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            for (auto it = _pendingRequests.begin(); it != _pendingRequests.end();) {
                if (it->second.sourceResource == resourceName) {
                    it = _pendingRequests.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }

    void Messages::Shutdown() {
        {
            std::scoped_lock lock(_handlersMutex);
            _handlers.clear();
        }
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            _pendingRequests.clear();
        }
        {
            std::scoped_lock lock(_responseQueueMutex);
            _responseQueue.clear();
        }
    }

} // namespace Framework::Scripting::Builtins
