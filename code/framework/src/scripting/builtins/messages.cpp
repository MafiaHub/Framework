/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "messages.h"
#include "../engine.h"
#include "../function_references.h"
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

    void Messages::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager) {
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

        target->Set(context, v8pp::to_v8(isolate, "Messages"), messagesObj).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("Messages", "Typed request and notification channel between local resources through the global Messages object.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("handle",
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("messageType", "string", false, "Message type unique within the receiving resource."),
                    v8pp::metadata::param("handler", "MessageHandler", false, "Handler invoked with the payload and a reply callback; the reply is ignored for notifications."),
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
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.handle requires 2 arguments: messageType, handler")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.handle: messageType must be a string")));
            return;
        }

        if (!args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.handle: handler must be a function")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Messages.handle: resource manager not available")));
            return;
        }

        std::string messageType         = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();
        std::string resourceName        = manager->ResolveResourceContext(isolate, handler);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Messages.handle: must be called from within a resource")));
            return;
        }

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
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.request requires at least 2 arguments: resourceName, messageType")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.request: resourceName must be a string")));
            return;
        }

        if (!args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.request: messageType must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Messages.request: resource manager not available")));
            return;
        }

        std::string targetResource = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string messageType    = v8pp::from_v8<std::string>(isolate, args[1]);
        std::string sourceResource = manager->ResolveResourceContext(isolate);

        v8::Local<v8::Value> payload = args.Length() > 2 ? args[2] : v8::Undefined(isolate).As<v8::Value>();

        // Create a Promise
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise            = resolver->GetPromise();

        // Check if target resource has a handler
        Handler handler;
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

            // Held by value, in the isolate it belongs to: the handler may live in another resource's runtime.
            handler.isolate = handlerIt->second.isolate;
            handler.function.Reset(handler.isolate, handlerIt->second.function);
        }

        // Generate request ID and store pending request
        uint64_t requestId;
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            requestId = _nextRequestId++;
            _pendingRequests.try_emplace(requestId, requestId, v8::Global<v8::Promise::Resolver>(isolate, resolver), sourceResource, targetResource, isolate, context);
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
                pendingResponse.isolate = replyIsolate;
                _responseQueue.push_back(std::move(pendingResponse));
            }
        };

        v8::Local<v8::BigInt> requestIdData = v8::BigInt::NewFromUnsigned(isolate, requestId);
        v8::Local<v8::Function> replyFn     = v8::Function::New(context, replyCallback, requestIdData).ToLocalChecked();

        // Save current resource context and set to target resource
        std::string previousContext = manager->GetCurrentResourceContext();
        manager->SetCurrentResourceContext(targetResource);

        // Call the handler with (payload, reply)
        v8::TryCatch tryCatch(isolate);
        v8::MaybeLocal<v8::Value> result = CallHandler(isolate, context, manager, handler, payload, replyFn);

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
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.send requires at least 2 arguments: resourceName, messageType")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.send: resourceName must be a string")));
            return;
        }

        if (!args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.send: messageType must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());

        std::string targetResource = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string messageType    = v8pp::from_v8<std::string>(isolate, args[1]);

        v8::Local<v8::Value> payload = args.Length() > 2 ? args[2] : v8::Undefined(isolate).As<v8::Value>();

        // Find handler
        Handler handler;
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

            handler.isolate = handlerIt->second.isolate;
            handler.function.Reset(handler.isolate, handlerIt->second.function);
        }

        // Create no-op reply function
        auto noOpReply                  = [](const v8::FunctionCallbackInfo<v8::Value> &) {};
        v8::Local<v8::Function> replyFn = v8::Function::New(context, noOpReply).ToLocalChecked();

        // Save current resource context and set to target resource
        std::string previousContext = manager ? manager->GetCurrentResourceContext() : "";
        if (manager) {
            manager->SetCurrentResourceContext(targetResource);
        }

        // Call the handler with (payload, reply)
        v8::TryCatch tryCatch(isolate);
        (void)CallHandler(isolate, context, manager, handler, payload, replyFn);

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

            // reply() ran in the requester's runtime (from another one it arrives through a reference), so the
            // response lives where the Promise does.
            Settle(it->second, [&](v8::Isolate *owner, v8::Local<v8::Context> ownerContext, v8::Local<v8::Promise::Resolver> resolver) {
                v8::Local<v8::Value> responseValue = response.response.Get(owner);
                if (response.isError) {
                    resolver->Reject(ownerContext, responseValue).Check();
                }
                else {
                    resolver->Resolve(ownerContext, responseValue).Check();
                }
            });

            _pendingRequests.erase(it);
        }
    }

    void Messages::CleanupResource(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &resourceName) {
        // Drop the stopped resource's registered handlers so its Global<Function>
        // handles are released now instead of lingering until Shutdown().
        {
            std::scoped_lock lock(_handlersMutex);
            _handlers.erase(resourceName);
        }

        // Settle and drop every pending request tied to the stopped resource:
        // - source: a request this resource made, whose reply it can no longer receive;
        // - target: a request another resource is still awaiting a reply from this one.
        // Reject the awaiting Promise first (so `await request(...)` rejects instead of hanging
        // forever) before erasing, then release the resolver Global.
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            for (auto it = _pendingRequests.begin(); it != _pendingRequests.end();) {
                PendingRequest &req = it->second;
                if (req.sourceResource == resourceName || req.targetResource == resourceName) {
                    // A request still in the table is not yet settled — ProcessPendingResponses
                    // settles and erases atomically under this same mutex — so rejecting always
                    // moves the awaiting Promise out of pending (and V8 ignores a reject on an
                    // already-settled promise anyway). After erasing, a late reply() finds nothing.
                    Settle(req, [&](v8::Isolate *owner, v8::Local<v8::Context> ownerContext, v8::Local<v8::Promise::Resolver> resolver) {
                        resolver->Reject(ownerContext, v8pp::to_v8(owner, "Messages.request: resource '" + resourceName + "' stopped before reply")).Check();
                    });
                    it = _pendingRequests.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }

    void Messages::ForgetIsolate(v8::Isolate *isolate) {
        {
            std::scoped_lock lock(_handlersMutex);
            for (auto &[resourceName, handlers] : _handlers) {
                std::erase_if(handlers, [&](const auto &entry) { return entry.second.isolate == isolate; });
            }
        }
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            std::erase_if(_pendingRequests, [&](const auto &entry) { return entry.second.isolate == isolate; });
        }
        {
            std::scoped_lock lock(_responseQueueMutex);
            std::erase_if(_responseQueue, [&](const PendingResponse &response) { return response.isolate == isolate; });
        }
    }

    v8::MaybeLocal<v8::Value> Messages::CallHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, ResourceManager *manager, const Handler &handler, v8::Local<v8::Value> payload, v8::Local<v8::Function> reply) {
        if (handler.isolate == isolate) {
            v8::Local<v8::Value> argv[2] = {payload, reply};
            return handler.function.Get(isolate)->Call(context, context->Global(), 2, argv);
        }

        // Only a throw matters to the caller; the answer goes through reply(), so the return value is never copied.
        FunctionReferences *references = manager != nullptr ? manager->GetFunctionReferences() : nullptr;
        if (references == nullptr) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Messages: the handler's resource is not reachable from here")));
            return {};
        }
        return references->Call(isolate, handler.isolate, handler.function, std::vector<v8::Local<v8::Value>> {payload, reply}, FunctionReferences::Returned::Outcome);
    }

    void Messages::Settle(PendingRequest &request, fu2::function_view<void(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Promise::Resolver>)> settle) {
        v8::Isolate *isolate = request.isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = request.context.Get(isolate);
        v8::Context::Scope contextScope(context);
        settle(isolate, context, request.resolver.Get(isolate));
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
