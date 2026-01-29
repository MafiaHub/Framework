#include "messages.h"
#include "../resource/resource_manager.h"

#include <logging/logger.h>

namespace Framework::Scripting {

    std::map<std::string, std::map<std::string, v8::Global<v8::Function>>> Messages::_handlers;
    std::mutex Messages::_handlersMutex;
    std::map<uint64_t, Messages::PendingRequest> Messages::_pendingRequests;
    std::mutex Messages::_pendingRequestsMutex;
    std::vector<Messages::PendingResponse> Messages::_responseQueue;
    std::mutex Messages::_responseQueueMutex;
    uint64_t Messages::_nextRequestId = 1;
    ResourceManager *Messages::_resourceManager = nullptr;

    void Messages::Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            ResourceManager *resourceManager) {
        _resourceManager = resourceManager;

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
    }

    void Messages::HandleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle requires 2 arguments: messageType, handler")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle: messageType must be a string")));
            return;
        }

        if (!args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle: handler must be a function")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle: resource manager not available")));
            return;
        }

        std::string messageType = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string resourceName = manager->GetCurrentResourceContext();

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.handle: must be called from within a resource")));
            return;
        }

        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            _handlers[resourceName][messageType].Reset(isolate, handler);
        }
    }

    void Messages::RequestCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.request requires at least 2 arguments: resourceName, messageType")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.request: resourceName must be a string")));
            return;
        }

        if (!args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.request: messageType must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.request: resource manager not available")));
            return;
        }

        std::string targetResource = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string messageType = v8pp::from_v8<std::string>(isolate, args[1]);
        std::string sourceResource = manager->GetCurrentResourceContext();

        v8::Local<v8::Value> payload = args.Length() > 2 ? args[2] : v8::Undefined(isolate).As<v8::Value>();

        // Create a Promise
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise = resolver->GetPromise();

        // Check if target resource has a handler
        v8::Global<v8::Function> handler;
        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
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

            handler.Reset(isolate, handlerIt->second.Get(isolate));
        }

        // Generate request ID and store pending request
        uint64_t requestId;
        {
            std::lock_guard<std::mutex> lock(_pendingRequestsMutex);
            requestId = _nextRequestId++;
            _pendingRequests[requestId] = {requestId, v8::Global<v8::Promise::Resolver>(isolate, resolver), sourceResource};
        }

        // Create reply function
        auto replyCallback = [](const v8::FunctionCallbackInfo<v8::Value> &replyArgs) {
            v8::Isolate *replyIsolate = replyArgs.GetIsolate();
            v8::HandleScope replyScope(replyIsolate);
            v8::Local<v8::Context> replyContext = replyIsolate->GetCurrentContext();

            uint64_t reqId = reinterpret_cast<uintptr_t>(replyArgs.Data().As<v8::External>()->Value());

            v8::Local<v8::Value> response = replyArgs.Length() > 0 ? replyArgs[0] : v8::Undefined(replyIsolate).As<v8::Value>();

            {
                std::lock_guard<std::mutex> lock(_responseQueueMutex);
                PendingResponse pendingResponse;
                pendingResponse.requestId = reqId;
                pendingResponse.response.Reset(replyIsolate, response);
                pendingResponse.isError = false;
                _responseQueue.push_back(std::move(pendingResponse));
            }
        };

        v8::Local<v8::External> requestIdData = v8::External::New(isolate, reinterpret_cast<void *>(requestId));
        v8::Local<v8::Function> replyFn = v8::Function::New(context, replyCallback, requestIdData).ToLocalChecked();

        // Call the handler with (payload, reply)
        v8::Local<v8::Function> handlerFn = handler.Get(isolate);
        v8::Local<v8::Value> argv[2] = {payload, replyFn};

        // Save current resource context and set to target resource
        std::string previousContext = manager->GetCurrentResourceContext();
        manager->SetCurrentResourceContext(targetResource);

        v8::TryCatch tryCatch(isolate);
        v8::MaybeLocal<v8::Value> result = handlerFn->Call(context, context->Global(), 2, argv);

        if (tryCatch.HasCaught()) {
            v8::String::Utf8Value error(isolate, tryCatch.Exception());
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "[{}] Message handler '{}' error: {}",
                targetResource, messageType, *error ? *error : "Unknown error");

            // Reject the promise
            {
                std::lock_guard<std::mutex> lock(_pendingRequestsMutex);
                auto it = _pendingRequests.find(requestId);
                if (it != _pendingRequests.end()) {
                    it->second.resolver.Get(isolate)->Reject(context, tryCatch.Exception()).Check();
                    _pendingRequests.erase(it);
                }
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
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.send requires at least 2 arguments: resourceName, messageType")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.send: resourceName must be a string")));
            return;
        }

        if (!args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "messages.send: messageType must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());

        std::string targetResource = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string messageType = v8pp::from_v8<std::string>(isolate, args[1]);

        v8::Local<v8::Value> payload = args.Length() > 2 ? args[2] : v8::Undefined(isolate).As<v8::Value>();

        // Find handler
        v8::Global<v8::Function> handler;
        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            auto resourceIt = _handlers.find(targetResource);
            if (resourceIt == _handlers.end()) {
                return; // Silently ignore
            }

            auto handlerIt = resourceIt->second.find(messageType);
            if (handlerIt == resourceIt->second.end()) {
                return; // Silently ignore
            }

            handler.Reset(isolate, handlerIt->second.Get(isolate));
        }

        // Create no-op reply function
        auto noOpReply = [](const v8::FunctionCallbackInfo<v8::Value> &) {};
        v8::Local<v8::Function> replyFn = v8::Function::New(context, noOpReply).ToLocalChecked();

        // Call the handler with (payload, reply)
        v8::Local<v8::Function> handlerFn = handler.Get(isolate);
        v8::Local<v8::Value> argv[2] = {payload, replyFn};

        // Save current resource context and set to target resource
        std::string previousContext = manager ? manager->GetCurrentResourceContext() : "";
        if (manager) {
            manager->SetCurrentResourceContext(targetResource);
        }

        v8::TryCatch tryCatch(isolate);
        handlerFn->Call(context, context->Global(), 2, argv);

        if (tryCatch.HasCaught()) {
            v8::String::Utf8Value error(isolate, tryCatch.Exception());
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "[{}] Message handler '{}' error: {}",
                targetResource, messageType, *error ? *error : "Unknown error");
        }

        // Restore previous resource context
        if (manager) {
            manager->SetCurrentResourceContext(previousContext);
        }
    }

    void Messages::ProcessPendingResponses(v8::Isolate *isolate, v8::Local<v8::Context> context) {
        std::vector<PendingResponse> responses;
        {
            std::lock_guard<std::mutex> lock(_responseQueueMutex);
            responses = std::move(_responseQueue);
            _responseQueue.clear();
        }

        for (auto &response : responses) {
            std::lock_guard<std::mutex> lock(_pendingRequestsMutex);
            auto it = _pendingRequests.find(response.requestId);
            if (it == _pendingRequests.end()) {
                continue;
            }

            v8::Local<v8::Promise::Resolver> resolver = it->second.resolver.Get(isolate);
            v8::Local<v8::Value> responseValue = response.response.Get(isolate);

            if (response.isError) {
                resolver->Reject(context, responseValue).Check();
            } else {
                resolver->Resolve(context, responseValue).Check();
            }

            _pendingRequests.erase(it);
        }
    }

    void Messages::Shutdown() {
        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            _handlers.clear();
        }
        {
            std::lock_guard<std::mutex> lock(_pendingRequestsMutex);
            _pendingRequests.clear();
        }
        {
            std::lock_guard<std::mutex> lock(_responseQueueMutex);
            _responseQueue.clear();
        }
        _resourceManager = nullptr;
    }

} // namespace Framework::Scripting
