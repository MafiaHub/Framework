/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "messages.h"
#include "../bind/v8_callable.h"
#include "../callback_scope.h"
#include "../engine.h"
#include "../engine_helpers.h"
#include "../function_references.h"
#include "../lua/lua_runtime.h"
#include "../resource/resource_manager.h"
#include "../resource_monitor.h"
#include "../script_log.h"
#include "../scripting_catalog.h"
#include "../value_transfer.h"

#include <logging/logger.h>

#include <utility>

namespace Framework::Scripting::Builtins {

    std::map<std::string, std::map<std::string, Messages::Handler>> Messages::_handlers;
    std::mutex Messages::_handlersMutex;
    std::map<uint64_t, Messages::PendingRequest> Messages::_pendingRequests;
    std::mutex Messages::_pendingRequestsMutex;
    std::vector<Messages::PendingResponse> Messages::_responseQueue;
    std::mutex Messages::_responseQueueMutex;
    uint64_t Messages::_nextRequestId = 1;
    std::map<const ResourceManager *, std::shared_ptr<Messages::ManagerLink>> Messages::_managers;
    bool Messages::_luaRequestsUnchecked = false;

    namespace {
        constexpr const char *kNoResource = "Target resource not found";
        constexpr const char *kNoHandler  = "No handler for message type";

        std::string StoppedBeforeReply(const std::string &resourceName) {
            return "Messages.request: resource '" + resourceName + "' stopped before reply";
        }

        void LogHandlerError(const std::string &resourceName, const std::string &messageType, const std::string &error) {
            LogScriptError(resourceName, "Message handler '" + messageType + "' error: " + error);
        }
    } // namespace

    // The `reply` of a handler that is called through its callable: a function of the host, which arrives in the
    // handler's runtime as a function of its language (Call::FromTransfer) and takes a copy of the answer. It
    // belongs to no runtime, so the handler may keep it and call it whenever it likes: once the request is
    // answered, or gone with the resource that made it, it does nothing, as the reply function of JavaScript does.
    class Messages::ReplyFunction final: public Bind::ScriptCallable {
      public:
        // `requestId` is 0 for the reply of a notification, which nobody waits for.
        ReplyFunction(uint64_t requestId, std::string requester): _requestId(requestId), _requester(std::move(requester)) {}

        // Not JavaScript: whoever is handed it on must not look for a function of an isolate behind it.
        Language GetLanguage() const override {
            return Language::Lua;
        }

        std::string ResourceName() const override {
            return _requester;
        }

        bool IsAlive() const override {
            return true;
        }

        bool Equals(const Bind::ScriptCallable &other) const override {
            return this == &other;
        }

        void Invoke(const std::vector<TransferredValue> &args, Bind::ReturnMode, Bind::Completion done) override {
            if (_requestId != 0) {
                QueueReply(_requestId, args.empty() ? TransferredValue {} : args.front());
            }
            if (done) {
                done({});
            }
        }

      private:
        uint64_t _requestId;
        std::string _requester;
    };

    // The functions of the definition: one body for both languages. A JavaScript request or notification to a
    // JavaScript handler alone speaks V8, to hand the payload and the reply function on as it always did.
    struct Messages::Script final {
        static void Handle(Bind::Call &call) {
            if (call.ArgCount() < 2) {
                call.ThrowTypeError("Messages.handle requires 2 arguments: messageType, handler");
                return;
            }

            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Messages.handle: messageType must be a string");
                return;
            }

            if (!call.Arg(1).IsFunction()) {
                call.ThrowTypeError("Messages.handle: handler must be a function");
                return;
            }

            ResourceManager *manager = call.GetResourceManager();
            if (manager == nullptr) {
                call.ThrowError("Messages.handle: resource manager not available");
                return;
            }

            const std::string messageType  = call.Arg(0).AsString();
            const std::string resourceName = call.OwnerOf(call.Arg(1));

            if (resourceName.empty()) {
                call.ThrowError("Messages.handle: must be called from within a resource");
                return;
            }

            Handler entry;
            entry.callable = call.Arg(1).ToCallable();
            if (call.GetLanguage() == Language::JavaScript) {
                entry.isolate = v8::Isolate::GetCurrent();
                entry.function.Reset(entry.isolate, Bind::V8ValueOf(call, call.Arg(1)).As<v8::Function>());
            }

            // The handler it replaces is let go of once the table is unlocked.
            {
                std::scoped_lock lock(_handlersMutex);
                entry.manager = LinkOf(manager);
                std::swap(_handlers[resourceName][messageType], entry);
            }
        }

        static void Request(Bind::Call &call) {
            if (call.ArgCount() < 2) {
                call.ThrowTypeError("Messages.request requires at least 2 arguments: resourceName, messageType");
                return;
            }

            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Messages.request: resourceName must be a string");
                return;
            }

            if (!call.Arg(1).IsString()) {
                call.ThrowTypeError("Messages.request: messageType must be a string");
                return;
            }

            ResourceManager *manager = call.GetResourceManager();
            if (!manager) {
                call.ThrowError("Messages.request: resource manager not available");
                return;
            }

            const std::string targetResource = call.Arg(0).AsString();
            const std::string messageType    = call.Arg(1).AsString();

            if (call.GetLanguage() == Language::JavaScript) {
                RequestFromJavaScript(call, manager, targetResource, messageType);
                return;
            }

            // Copied before anything else: a payload that cannot cross is the caller's mistake, whoever would
            // have handled it.
            auto payload = call.Arg(2).ToTransfer();
            if (!payload) {
                call.ThrowTypeError("Messages.request: payload: " + payload.GetError());
                return;
            }

            // What rejects the Promise of a JavaScript request raises here, with the same text.
            Bind::Deferred waiter = call.Defer();

            Handler handler;
            switch (FindHandler(targetResource, messageType, false, handler)) {
            case Lookup::NoResource: waiter.Reject(kNoResource); return;
            case Lookup::NoHandler: waiter.Reject(kNoHandler); return;
            case Lookup::Found: break;
            }

            const std::string sourceResource = call.ResourceName();
            uint64_t requestId;
            {
                std::scoped_lock lock(_pendingRequestsMutex);
                requestId = _nextRequestId++;
                _pendingRequests.try_emplace(requestId, requestId, waiter, sourceResource, targetResource);
                _luaRequestsUnchecked = true;
            }

            if (const auto error = Dispatch(handler, targetResource, messageType, requestId, sourceResource, std::move(payload).GetValue())) {
                HandlerFailed(handler.manager, targetResource, messageType, requestId, *error);
            }
        }

        static void RequestFromJavaScript(Bind::Call &call, ResourceManager *manager, const std::string &targetResource, const std::string &messageType) {
            v8::Isolate *isolate           = v8::Isolate::GetCurrent();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            std::string sourceResource     = manager->ResolveResourceContext(isolate);

            v8::Local<v8::Value> payload = call.ArgCount() > 2 ? Bind::V8ValueOf(call, call.Arg(2)) : v8::Undefined(isolate).As<v8::Value>();

            // Create a Promise
            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            v8::Local<v8::Promise> promise            = resolver->GetPromise();
            call.Return(Bind::V8ValueIn(call, promise));

            // Check if target resource has a handler
            Handler handler;
            switch (FindHandler(targetResource, messageType, true, handler)) {
            case Lookup::NoResource: resolver->Reject(context, v8pp::to_v8(isolate, kNoResource)).Check(); return;
            case Lookup::NoHandler: resolver->Reject(context, v8pp::to_v8(isolate, kNoHandler)).Check(); return;
            case Lookup::Found: break;
            }

            // A Lua handler: called through its callable, with a copy of the payload made before the request
            // exists, so one that cannot cross leaves nothing behind.
            if (handler.isolate == nullptr) {
                TransferredValue copy;
                {
                    // A getter of the script may throw while the payload is read: the copy says so.
                    v8::TryCatch tryCatch(isolate);
                    auto copied = ValueTransfer::Copy(isolate, context, payload, Bind::V8CallableTransfer());
                    if (!copied) {
                        resolver->Reject(context, v8::Exception::TypeError(v8pp::to_v8(isolate, "Messages.request: payload: " + copied.GetError()))).Check();
                        return;
                    }
                    copy = std::move(copied).GetValue();
                }

                uint64_t requestId;
                {
                    std::scoped_lock lock(_pendingRequestsMutex);
                    requestId = _nextRequestId++;
                    _pendingRequests.try_emplace(requestId, requestId, v8::Global<v8::Promise::Resolver>(isolate, resolver), sourceResource, targetResource, isolate, context);
                }

                if (const auto error = Dispatch(handler, targetResource, messageType, requestId, sourceResource, std::move(copy))) {
                    // Rejected here, in the call that made the Promise, as for a JavaScript handler that throws.
                    if (const RequestNode request = TakeUnanswered(requestId); !request.empty()) {
                        const bool stopped = *error == Bind::kRuntimeStopped;
                        if (!stopped) {
                            LogHandlerError(targetResource, messageType, *error);
                        }
                        const v8::Local<v8::String> text = v8pp::to_v8(isolate, stopped ? StoppedBeforeReply(targetResource) : *error).As<v8::String>();
                        resolver->Reject(context, stopped ? text.As<v8::Value>() : v8::Exception::Error(text)).Check();
                    }
                    else {
                        ReportUnawaited(handler.manager, targetResource, messageType, *error);
                    }
                }
                return;
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
            v8::MaybeLocal<v8::Value> result = CallHandler(isolate, context, manager, handler, payload, replyFn, messageType);

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
        }

        static void Send(Bind::Call &call) {
            if (call.ArgCount() < 2) {
                call.ThrowTypeError("Messages.send requires at least 2 arguments: resourceName, messageType");
                return;
            }

            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Messages.send: resourceName must be a string");
                return;
            }

            if (!call.Arg(1).IsString()) {
                call.ThrowTypeError("Messages.send: messageType must be a string");
                return;
            }

            ResourceManager *manager = call.GetResourceManager();

            const std::string targetResource = call.Arg(0).AsString();
            const std::string messageType    = call.Arg(1).AsString();

            if (call.GetLanguage() == Language::JavaScript) {
                SendFromJavaScript(call, manager, targetResource, messageType);
                return;
            }

            auto payload = call.Arg(2).ToTransfer();
            if (!payload) {
                call.ThrowTypeError("Messages.send: payload: " + payload.GetError());
                return;
            }

            Handler handler;
            if (FindHandler(targetResource, messageType, false, handler) != Lookup::Found) {
                return; // Silently ignore
            }

            if (const auto error = Dispatch(handler, targetResource, messageType, 0, call.ResourceName(), std::move(payload).GetValue())) {
                ReportUnawaited(handler.manager, targetResource, messageType, *error);
            }
        }

        static void SendFromJavaScript(Bind::Call &call, ResourceManager *manager, const std::string &targetResource, const std::string &messageType) {
            v8::Isolate *isolate           = v8::Isolate::GetCurrent();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            v8::Local<v8::Value> payload = call.ArgCount() > 2 ? Bind::V8ValueOf(call, call.Arg(2)) : v8::Undefined(isolate).As<v8::Value>();

            // Find handler
            Handler handler;
            if (FindHandler(targetResource, messageType, true, handler) != Lookup::Found) {
                return; // Silently ignore
            }

            // A Lua handler: called through its callable, with a copy of the payload. One that cannot cross is
            // logged, as it is for a JavaScript handler of another runtime.
            if (handler.isolate == nullptr) {
                TransferredValue copy;
                {
                    v8::TryCatch tryCatch(isolate);
                    auto copied = ValueTransfer::Copy(isolate, context, payload, Bind::V8CallableTransfer());
                    if (!copied) {
                        LogHandlerError(targetResource, messageType, "Messages.send: payload: " + copied.GetError());
                        return;
                    }
                    copy = std::move(copied).GetValue();
                }
                if (const auto error = Dispatch(handler, targetResource, messageType, 0, manager != nullptr ? manager->ResolveResourceContext(isolate) : std::string(), std::move(copy))) {
                    ReportUnawaited(handler.manager, targetResource, messageType, *error);
                }
                return;
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
            (void)CallHandler(isolate, context, manager, handler, payload, replyFn, messageType);

            if (tryCatch.HasCaught()) {
                v8::String::Utf8Value error(isolate, tryCatch.Exception());
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Message handler '{}' error: {}", targetResource, messageType, *error ? *error : "Unknown error");
            }

            // Restore previous resource context
            if (manager) {
                manager->SetCurrentResourceContext(previousContext);
            }
        }
    };

    const Bind::Namespace &Messages::Definition() {
        using Bind::Doc;
        using Bind::Type::Custom;

        static const Bind::Namespace definition = Bind::Namespace("Messages", "Typed request and notification channel between local resources through the global Messages object.")
                                                      .Function("handle", &Script::Handle,
                                                          Doc("Registers or replaces a message handler owned by the calling resource.")
                                                              .Param("messageType", Bind::TypeOf<std::string>(), "Message type unique within the receiving resource.")
                                                              .Param("handler", Custom("MessageHandler", "fun(payload: any, reply: fun(response?: any))"), "Handler invoked with the payload and a reply callback; the reply is ignored for notifications."))
                                                      .Function("request", &Script::Request,
                                                          Doc("Sends a request to another local resource and waits for its handler to call reply.")
                                                              .Param("resourceName", Bind::TypeOf<std::string>(), "Destination running resource.")
                                                              .Param("messageType", Bind::TypeOf<std::string>(), "Handler type registered by the destination.")
                                                              .OptionalParam("payload", Custom("unknown", "any"), "Optional payload delivered to the handler.")
                                                              .ReturnType(Custom("Promise<unknown>", "any"))
                                                              .Returns("Promise resolved with the reply value or rejected when delivery or handling fails."))
                                                      .Function("send", &Script::Send,
                                                          Doc("Sends a fire-and-forget notification to another local resource.")
                                                              .Param("resourceName", Bind::TypeOf<std::string>(), "Destination running resource.")
                                                              .Param("messageType", Bind::TypeOf<std::string>(), "Handler type registered by the destination.")
                                                              .OptionalParam("payload", Custom("unknown", "any"), "Optional payload delivered to the handler."));
        return definition;
    }

    void Messages::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager) {
        // The functions carry no manager of their own: a call asks the one of its runtime.
        if (resourceManager != nullptr) {
            Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, target, GetScriptingCatalog(isolate), ScriptingSide(isolate));
    }

    void Messages::RegisterLua(LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

    void Messages::ProcessPendingResponses(v8::Isolate *, v8::Local<v8::Context>) {
        // Each reply is delivered in the runtime of its requester, whichever the caller stands in.
        ProcessPendingResponses();
    }

    void Messages::ProcessPendingResponses() {
        // A request made from Lua since the last time may have had nowhere to wait: its call has raised by now.
        bool unchecked = false;
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            unchecked = std::exchange(_luaRequestsUnchecked, false);
        }
        if (unchecked) {
            DropAbandonedRequests();
        }

        std::vector<PendingResponse> responses;
        {
            std::scoped_lock lock(_responseQueueMutex);
            responses = std::move(_responseQueue);
            _responseQueue.clear();
        }

        for (auto &response : responses) {
            // Out of the table under the lock, settled outside it: settling runs the requester's continuations, which
            // may make requests of their own. A late reply() finds nothing either way.
            RequestNode request;
            {
                std::scoped_lock lock(_pendingRequestsMutex);
                request = _pendingRequests.extract(response.requestId);
            }
            if (request.empty()) {
                continue;
            }

            // A request made from Lua: its coroutine goes on with a rebuild of the copy, here.
            if (request.mapped().isolate == nullptr) {
                request.mapped().waiter.Resolve(std::move(response.copy));
                continue;
            }

            // reply() ran in the requester's runtime (from another one it arrives through a reference), so the
            // response lives where the Promise does. The reply of a Lua handler is a copy, rebuilt there.
            Settle(request.mapped(), [&](v8::Isolate *owner, v8::Local<v8::Context> ownerContext, v8::Local<v8::Promise::Resolver> resolver) {
                if (response.isolate == nullptr) {
                    v8::TryCatch tryCatch(owner);
                    v8::Local<v8::Value> rebuilt;
                    if (ValueTransfer::Rebuild(owner, ownerContext, response.copy).ToLocal(&rebuilt)) {
                        resolver->Resolve(ownerContext, rebuilt).Check();
                    }
                    else {
                        resolver->Reject(ownerContext, tryCatch.HasCaught() ? tryCatch.Exception() : v8::Exception::Error(v8pp::to_v8(owner, "Messages.request: the reply could not be delivered"))).Check();
                    }
                    return;
                }
                v8::Local<v8::Value> responseValue = response.response.Get(owner);
                if (response.isError) {
                    resolver->Reject(ownerContext, responseValue).Check();
                }
                else {
                    resolver->Resolve(ownerContext, responseValue).Check();
                }
            });
        }
    }

    void Messages::CleanupResource(v8::Isolate *, v8::Local<v8::Context>, const std::string &resourceName) {
        // Each Promise is settled in the runtime of its requester, whichever the caller stands in.
        CleanupResource(resourceName);
    }

    void Messages::CleanupResource(const std::string &resourceName) {
        // Drop the stopped resource's registered handlers so its Global<Function>
        // handles are released now instead of lingering until Shutdown().
        {
            std::map<std::string, std::map<std::string, Handler>>::node_type dropped;
            std::scoped_lock lock(_handlersMutex);
            dropped = _handlers.extract(resourceName);
        }

        // Settle and drop every pending request tied to the stopped resource:
        // - source: a request this resource made, whose reply it can no longer receive;
        // - target: a request another resource is still awaiting a reply from this one.
        // A request still in the table is not yet settled — ProcessPendingResponses takes a request
        // out under this same mutex before settling it — so rejecting always moves the awaiting
        // Promise out of pending (so `await request(...)` rejects instead of hanging forever), and a
        // late reply() finds nothing. Settled outside the lock: the requester's continuations run then.
        std::vector<RequestNode> stopped;
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            for (auto it = _pendingRequests.begin(); it != _pendingRequests.end();) {
                const PendingRequest &req = it->second;
                if (req.sourceResource == resourceName || req.targetResource == resourceName) {
                    stopped.push_back(_pendingRequests.extract(it++));
                }
                else {
                    ++it;
                }
            }
        }
        for (auto &request : stopped) {
            Reject(request.mapped(), StoppedBeforeReply(resourceName), false);
        }
    }

    void Messages::ForgetIsolate(v8::Isolate *isolate) {
        {
            std::scoped_lock lock(_handlersMutex);
            for (auto &[resourceName, handlers] : _handlers) {
                std::erase_if(handlers, [&](const auto &entry) {
                    return entry.second.isolate == isolate;
                });
            }
        }
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            std::erase_if(_pendingRequests, [&](const auto &entry) {
                return entry.second.isolate == isolate;
            });
        }
        {
            std::scoped_lock lock(_responseQueueMutex);
            std::erase_if(_responseQueue, [&](const PendingResponse &response) {
                return response.isolate == isolate;
            });
        }
    }

    Messages::Lookup Messages::FindHandler(const std::string &targetResource, const std::string &messageType, bool javaScript, Handler &handler) {
        std::scoped_lock lock(_handlersMutex);
        auto resourceIt = _handlers.find(targetResource);
        if (resourceIt == _handlers.end()) {
            return Lookup::NoResource;
        }

        auto handlerIt = resourceIt->second.find(messageType);
        if (handlerIt == resourceIt->second.end()) {
            return Lookup::NoHandler;
        }

        Handler &entry = handlerIt->second;
        if (javaScript && entry.isolate != nullptr) {
            handler.isolate = entry.isolate;
            handler.function.Reset(entry.isolate, entry.function);
            handler.callable = entry.callable;
            handler.manager  = entry.manager;
            return Lookup::Found;
        }

        // Called through its callable from here on, which a handler whose runtime went has no use for: the caller
        // lets go of it once the table is unlocked.
        if (entry.callable == nullptr || !entry.callable->IsAlive()) {
            handler = std::move(entry);
            resourceIt->second.erase(handlerIt);
            return Lookup::NoHandler;
        }
        handler.callable = entry.callable;
        handler.manager  = entry.manager;
        return Lookup::Found;
    }

    std::optional<std::string> Messages::Dispatch(const Handler &handler, const std::string &targetResource, const std::string &messageType, uint64_t requestId, const std::string &sourceResource, TransferredValue payload) {
        std::vector<TransferredValue> args(2);
        args[0]          = std::move(payload);
        args[1].kind     = TransferredValue::Kind::Function;
        args[1].callable = std::make_shared<ReplyFunction>(requestId, sourceResource);

        // What the call and the completion share: whether the call is still under way, and what the handler failed
        // with while it was.
        struct Told final {
            bool returned = false;
            std::optional<std::string> error;
        };
        const auto told = std::make_shared<Told>();

        // A JavaScript handler that is slow is reported under its own name, as when JavaScript calls it.
        v8::Isolate *isolate = handler.callable->GetLanguage() == Language::JavaScript ? static_cast<const Bind::V8Callable &>(*handler.callable).Isolate() : nullptr;
        auto *monitor        = isolate != nullptr ? ResourceMonitor::Current() : nullptr;
        const double charged = monitor ? monitor->ChargedMs(isolate, ResourceMonitor::Clock::now()) : 0.0;

        handler.callable->Invoke(args, Bind::ReturnMode::Outcome, [told, manager = handler.manager, targetResource, messageType, requestId](Bind::CallOutcome &&outcome) {
            if (!outcome.threw) {
                return;
            }
            if (!told->returned) {
                told->error = std::move(outcome.error);
                return;
            }
            HandlerFailed(manager, targetResource, messageType, requestId, outcome.error);
        });
        told->returned = true;

        if (monitor) {
            monitor->ReportOwnTime(isolate, "message handler for", messageType, charged, ResourceMonitor::Clock::now());
        }
        return std::move(told->error);
    }

    void Messages::QueueReply(uint64_t requestId, TransferredValue response) {
        {
            std::scoped_lock lock(_pendingRequestsMutex);
            auto it = _pendingRequests.find(requestId);
            if (it == _pendingRequests.end()) {
                return; // Request no longer exists
            }
            if (it->second.consumed.exchange(true)) {
                return; // Already consumed, ignore subsequent calls
            }
        }

        std::scoped_lock lock(_responseQueueMutex);
        PendingResponse pendingResponse;
        pendingResponse.requestId = requestId;
        pendingResponse.isError   = false;
        pendingResponse.copy      = std::move(response);
        _responseQueue.push_back(std::move(pendingResponse));
    }

    Messages::RequestNode Messages::TakeUnanswered(uint64_t requestId) {
        std::scoped_lock lock(_pendingRequestsMutex);
        auto it = _pendingRequests.find(requestId);
        if (it == _pendingRequests.end() || it->second.consumed.exchange(true)) {
            return {};
        }
        return _pendingRequests.extract(it);
    }

    void Messages::HandlerFailed(const std::shared_ptr<ManagerLink> &manager, const std::string &targetResource, const std::string &messageType, uint64_t requestId, const std::string &error) {
        RequestNode request = requestId != 0 ? TakeUnanswered(requestId) : RequestNode();
        if (request.empty()) {
            ReportUnawaited(manager, targetResource, messageType, error);
            return;
        }

        // A handler whose runtime went while the request waited on it reads as the resource stopping does.
        if (error == Bind::kRuntimeStopped) {
            Reject(request.mapped(), StoppedBeforeReply(targetResource), false);
            return;
        }
        LogHandlerError(targetResource, messageType, error);
        Reject(request.mapped(), error, true);
    }

    void Messages::ReportUnawaited(const std::shared_ptr<ManagerLink> &manager, const std::string &targetResource, const std::string &messageType, const std::string &error) {
        if (error == Bind::kRuntimeStopped) {
            return;
        }
        // Queued, not handled here, where a script may be executing: the manager acts on it at its next upkeep.
        if (manager != nullptr && manager->manager != nullptr) {
            manager->manager->ReportHandlerFailure(targetResource, "Message handler '" + messageType + "' error: " + error);
            return;
        }
        LogHandlerError(targetResource, messageType, error);
    }

    void Messages::Reject(PendingRequest &request, const std::string &message, bool asError) {
        if (request.isolate == nullptr) {
            request.waiter.Reject(message);
            return;
        }
        Settle(request, [&](v8::Isolate *owner, v8::Local<v8::Context> ownerContext, v8::Local<v8::Promise::Resolver> resolver) {
            const v8::Local<v8::String> text = v8pp::to_v8(owner, message).As<v8::String>();
            resolver->Reject(ownerContext, asError ? v8::Exception::Error(text) : text.As<v8::Value>()).Check();
        });
    }

    v8::MaybeLocal<v8::Value> Messages::CallHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, ResourceManager *manager, const Handler &handler, v8::Local<v8::Value> payload, v8::Local<v8::Function> reply, std::string_view messageType) {
        auto *monitor        = ResourceMonitor::Current();
        const double charged = monitor ? monitor->ChargedMs(handler.isolate, ResourceMonitor::Clock::now()) : 0.0;
        v8::MaybeLocal<v8::Value> result;
        if (handler.isolate == isolate) {
            ResourceMonitor::Scope scope(isolate);
            v8::Local<v8::Value> argv[2] = {payload, reply};
            result                       = handler.function.Get(isolate)->Call(context, context->Global(), 2, argv);
        }
        else {
            // Only a throw matters to the caller; the answer goes through reply(), so the return value is never copied.
            FunctionReferences *references = manager != nullptr ? manager->GetFunctionReferences() : nullptr;
            if (references == nullptr) {
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Messages: the handler's resource is not reachable from here")));
                return {};
            }
            result = references->Call(isolate, handler.isolate, handler.function, std::vector<v8::Local<v8::Value>> {payload, reply}, FunctionReferences::Returned::Outcome);
        }
        if (monitor) {
            monitor->ReportOwnTime(handler.isolate, "message handler for", messageType, charged, ResourceMonitor::Clock::now());
        }
        return result;
    }

    void Messages::Settle(PendingRequest &request, fu2::function_view<void(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Promise::Resolver>)> settle) {
        v8::Isolate *isolate = request.isolate;
        // The requester's continuations run in here, so they are charged to it as its own time.
        auto *monitor        = ResourceMonitor::Current();
        const double charged = monitor ? monitor->ChargedMs(isolate, ResourceMonitor::Clock::now()) : 0.0;
        {
            ResourceMonitor::Scope scope(isolate);
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = request.context.Get(isolate);
            v8::Context::Scope contextScope(context);
            // The requester's continuations run as this closes, so no lock of this class may be held here.
            const auto callbackScope = OpenCallbackScope(isolate);
            settle(isolate, context, request.resolver.Get(isolate));
        }
        if (monitor) {
            monitor->ReportOwnTime(isolate, "continuations of a reply from", request.targetResource, charged, ResourceMonitor::Clock::now());
        }
    }

    void Messages::ForgetManager(const ResourceManager *manager) {
        // Let go of once the table is unlocked.
        std::vector<Handler> dropped;
        std::scoped_lock lock(_handlersMutex);
        const auto link = _managers.find(manager);
        if (link == _managers.end()) {
            return;
        }
        for (auto resourceIt = _handlers.begin(); resourceIt != _handlers.end();) {
            for (auto handlerIt = resourceIt->second.begin(); handlerIt != resourceIt->second.end();) {
                if (handlerIt->second.manager == link->second) {
                    dropped.push_back(std::move(handlerIt->second));
                    handlerIt = resourceIt->second.erase(handlerIt);
                }
                else {
                    ++handlerIt;
                }
            }
            resourceIt = resourceIt->second.empty() ? _handlers.erase(resourceIt) : std::next(resourceIt);
        }
        // A handler that is still running reports to nobody.
        link->second->manager = nullptr;
        _managers.erase(link);
    }

    std::shared_ptr<Messages::ManagerLink> Messages::LinkOf(ResourceManager *manager) {
        std::shared_ptr<ManagerLink> &link = _managers[manager];
        if (link == nullptr) {
            link          = std::make_shared<ManagerLink>();
            link->manager = manager;
        }
        return link;
    }

    void Messages::DropAbandonedRequests() {
        // Let go of once the table is unlocked.
        std::vector<RequestNode> abandoned;
        std::scoped_lock lock(_pendingRequestsMutex);
        for (auto it = _pendingRequests.begin(); it != _pendingRequests.end();) {
            if (it->second.isolate == nullptr && !it->second.waiter.Awaited()) {
                abandoned.push_back(_pendingRequests.extract(it++));
            }
            else {
                ++it;
            }
        }
    }

    size_t Messages::GetPendingRequestCount() {
        std::scoped_lock lock(_pendingRequestsMutex);
        return _pendingRequests.size();
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
