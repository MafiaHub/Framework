/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "events.h"
#include "../engine_helpers.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

namespace Framework::Scripting {

    // Context for AllSettled callback - holds resolver, error message, and owner for cleanup
    // Defined early so destructor can access it
    struct Events::AllSettledCallbackData {
        v8::Global<v8::Promise::Resolver> resolver;
        std::string errorMessage;
        Events *owner;                       // For removing from pending set on completion
        std::atomic<bool> cancelled {false}; // Set when Events is destroyed
    };

    Events::~Events() {
        // Invalidate the callback context so any outstanding unsubscribe
        // lambdas holding a reference will safely bail out
        if (_callbackContext) {
            _callbackContext->valid.store(false, std::memory_order_release);
        }

        // Mark all pending AllSettled callbacks as cancelled so any outstanding
        // promise then-handlers will safely bail out. The shared_ptr ensures
        // the data stays alive until the last reference (this set or the
        // then-handler) releases it.
        std::scoped_lock lock(_pendingCallbacksMutex);
        for (const auto &data : _pendingCallbacks) {
            data->cancelled.store(true, std::memory_order_release);
            data->resolver.Reset();
        }
        _pendingCallbacks.clear();
    }

    void Events::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager, bool isClient) {
        // Reuse the context across repeated Register() calls; never reallocate. Its address is
        // baked into non-owning v8::Externals (template data and on() unsubscribe closures) that
        // outlive this call, so replacing it would dangle them. Contents are invariant anyway.
        if (!_callbackContext) {
            _callbackContext = std::make_unique<CallbackContext>();
        }
        _callbackContext->events          = this;
        _callbackContext->resourceManager = resourceManager;
        _callbackContext->valid.store(true, std::memory_order_release);

        v8::Local<v8::Object> eventsObj     = v8::Object::New(isolate);
        v8::Local<v8::External> contextData = v8::External::New(isolate, _callbackContext.get());

        // on(eventName, handler) -> unsubscribe function
        v8::Local<v8::FunctionTemplate> onTmpl = v8::FunctionTemplate::New(isolate, OnCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "on"), onTmpl->GetFunction(context).ToLocalChecked()).Check();

        // once(eventName, handler)
        v8::Local<v8::FunctionTemplate> onceTmpl = v8::FunctionTemplate::New(isolate, OnceCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "once"), onceTmpl->GetFunction(context).ToLocalChecked()).Check();

        // off(eventName, handler)
        v8::Local<v8::FunctionTemplate> offTmpl = v8::FunctionTemplate::New(isolate, OffCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "off"), offTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emit(eventName, ...args) -> Promise
        v8::Local<v8::FunctionTemplate> emitTmpl = v8::FunctionTemplate::New(isolate, EmitCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "emit"), emitTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emitTo(resourceName, eventName, ...args) -> Promise
        v8::Local<v8::FunctionTemplate> emitToTmpl = v8::FunctionTemplate::New(isolate, EmitToCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "emitTo"), emitToTmpl->GetFunction(context).ToLocalChecked()).Check();

        // onLocal(eventName, handler)
        v8::Local<v8::FunctionTemplate> onLocalTmpl = v8::FunctionTemplate::New(isolate, OnLocalCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "onLocal"), onLocalTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emitLocal(eventName, ...args) -> Promise
        v8::Local<v8::FunctionTemplate> emitLocalTmpl = v8::FunctionTemplate::New(isolate, EmitLocalCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "emitLocal"), emitLocalTmpl->GetFunction(context).ToLocalChecked()).Check();

        // listenerCount(eventName) -> number
        v8::Local<v8::FunctionTemplate> countTmpl = v8::FunctionTemplate::New(isolate, ListenerCountCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "listenerCount"), countTmpl->GetFunction(context).ToLocalChecked()).Check();

        // onClient/onceClient/offClient — client-originated events (dispatched via EmitClient).
        // Backed by a table separate from on()/emit(), so a client can never target a native handler.
        v8::Local<v8::FunctionTemplate> onClientTmpl = v8::FunctionTemplate::New(isolate, OnClientCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "onClient"), onClientTmpl->GetFunction(context).ToLocalChecked()).Check();

        v8::Local<v8::FunctionTemplate> onceClientTmpl = v8::FunctionTemplate::New(isolate, OnceClientCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "onceClient"), onceClientTmpl->GetFunction(context).ToLocalChecked()).Check();

        v8::Local<v8::FunctionTemplate> offClientTmpl = v8::FunctionTemplate::New(isolate, OffClientCallback, contextData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "offClient"), offClientTmpl->GetFunction(context).ToLocalChecked()).Check();

        // Bridge, split by side: emitServer up (client), emitAllClients down (server).
        if (isClient) {
            v8::Local<v8::FunctionTemplate> emitServerTmpl = v8::FunctionTemplate::New(isolate, EmitServerCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "emitServer"), emitServerTmpl->GetFunction(context).ToLocalChecked()).Check();
        }
        else {
            v8::Local<v8::FunctionTemplate> emitAllTmpl = v8::FunctionTemplate::New(isolate, EmitAllClientsCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "emitAllClients"), emitAllTmpl->GetFunction(context).ToLocalChecked()).Check();
        }

        // Register as "Events" on target object
        target->Set(context, v8pp::to_v8(isolate, "Events"), eventsObj).Check();

        auto &metadata         = GetScriptingCatalog(isolate).global_object("Events", "Asynchronous resource event bus exposed as Core.Events.");
        const auto handlerDocs = [](const char *returnType, const char *description, const char *returnDescription = "") {
            return v8pp::metadata::docs(returnType,
                {
                    v8pp::metadata::param("eventName", "string", false, "Case-sensitive event name."),
                    v8pp::metadata::param("handler", "(...args: unknown[]) => unknown | Promise<unknown>", false, "Resource-owned callback invoked with the emitted arguments."),
                },
                description, returnDescription);
        };
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("on", handlerDocs("() => void", "Registers a persistent handler in the shared event namespace.", "Function that removes this exact subscription.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("once", handlerDocs("void", "Registers a handler that is removed before its first invocation.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("off", handlerDocs("void", "Removes a matching handler owned by the calling resource.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emit",
            v8pp::metadata::docs("Promise<void>", {v8pp::metadata::param("eventName", "string", false, "Shared event name."), v8pp::metadata::param("args", "unknown[]", true, "Arguments delivered to every matching handler.")},
                "Invokes every shared handler and waits for all synchronous and asynchronous results.", "Promise rejected with an AggregateError when one or more handlers fail.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitTo", v8pp::metadata::docs("Promise<void>",
                                                                                        {v8pp::metadata::param("resourceName", "string", false, "Destination running resource."), v8pp::metadata::param("eventName", "string", false, "Shared event name."),
                                                                                            v8pp::metadata::param("args", "unknown[]", true, "Arguments delivered to matching handlers owned by the destination.")},
                                                                                        "Invokes matching handlers belonging only to one resource.", "Promise rejected when one or more destination handlers fail.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("onLocal", handlerDocs("void", "Registers a handler in the calling resource's private local-event namespace.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitLocal",
            v8pp::metadata::docs("Promise<void>", {v8pp::metadata::param("eventName", "string", false, "Private local event name."), v8pp::metadata::param("args", "unknown[]", true, "Arguments delivered only to handlers owned by the calling resource.")},
                "Emits an event only within the calling resource.", "Promise rejected when one or more local handlers fail.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("listenerCount",
            v8pp::metadata::docs("number", {v8pp::metadata::param("eventName", "string", false, "Shared event name to inspect.")}, "Counts persistent and one-shot shared handlers across resources.", "Number of matching handlers.")));
        metadata.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("onClient", handlerDocs("() => void", "Registers a persistent server handler for events originating from clients; this namespace is isolated from native events.", "Function that removes this exact subscription.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("onceClient", handlerDocs("void", "Registers a one-shot server handler for a client-originated event.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("offClient", handlerDocs("void", "Removes a matching client-originated event handler owned by the calling resource.")));
        if (isClient) {
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitServer",
                v8pp::metadata::docs("void", {v8pp::metadata::param("eventName", "string", false, "Server-side client-event name."), v8pp::metadata::param("payload", "unknown", true, "Optional string payload sent verbatim; other values are JSON-serialized.")},
                    "Sends a named event from this client to the server's isolated onClient handlers.")));
        }
        else {
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitAllClients",
                v8pp::metadata::docs("void", {v8pp::metadata::param("eventName", "string", false, "Client shared-event name."), v8pp::metadata::param("payload", "unknown", true, "Optional string payload sent verbatim; other values are JSON-serialized.")},
                    "Broadcasts a named event to every connected client's shared Events handlers.")));
        }
    }

    // Helper to get resource context with three-tier resolution:
    // 1. Handler function's script origin (async-safe, root cause fix)
    // 2. Explicit context (set during synchronous resource loading)
    // 3. V8 call stack file paths (fallback for async ES modules)
    std::string GetResourceContextWithFallback(v8::Isolate *isolate, ResourceManager *manager, v8::Local<v8::Function> handlerFn = {}) {
        // 1. Try handler function's script origin (async-safe, root cause fix)
        if (!handlerFn.IsEmpty()) {
            std::string name = manager->GetResourceNameFromFunction(isolate, handlerFn);
            if (!name.empty()) {
                return name;
            }
        }

        // 2. Try explicit context (set during synchronous resource loading)
        std::string resourceName = manager->GetCurrentResourceContext();
        if (!resourceName.empty()) {
            return resourceName;
        }

        // 3. Fallback: extract resource name from V8 call stack file paths
        return manager->GetResourceContextFromStack(isolate);
    }

    std::map<std::string, std::vector<EventHandler>, std::less<>> &Events::HandlerTable(HandlerScope scope) {
        return scope == HandlerScope::Client ? _clientHandlers : _globalHandlers;
    }

    // Human-readable API name for error messages, given scope + variant.
    static const char *EventApiName(bool client, const char *base) {
        if (!client)
            return base;
        if (std::string_view(base) == "on")
            return "onClient";
        if (std::string_view(base) == "once")
            return "onceClient";
        return "offClient";
    }

    // Shared body for emitServer/emitAllClients: BroadcastRPC a named event + payload over the peer's
    // connections (a client peer reaches only the server, a server peer all clients). A string payload
    // is sent verbatim; any other value is JSON-serialized here. `api` names the caller in errors.
    static void BroadcastLuaEvent(const v8::FunctionCallbackInfo<v8::Value> &args, const char *api) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + " requires an eventName string")));
            return;
        }
        const std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        if (eventName.empty()) {
            return;
        }

        std::string payload;
        if (args.Length() >= 2 && !args[1]->IsUndefined() && !args[1]->IsNull()) {
            if (args[1]->IsString()) {
                payload = v8pp::from_v8<std::string>(isolate, args[1]);
            }
            else {
                v8::Local<v8::String> json;
                if (v8::JSON::Stringify(isolate->GetCurrentContext(), args[1]).ToLocal(&json)) {
                    payload = v8pp::from_v8<std::string>(isolate, json);
                }
            }
        }

        auto *peer = CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        Framework::Integrations::Shared::RPC::EmitScriptEvent ev;
        ev.FromParameters(eventName, payload);
        peer->BroadcastRPC(ev);
    }

    void Events::RegisterHandler(v8::Isolate *isolate, ResourceManager *manager, std::string_view eventName, v8::Local<v8::Function> handler, bool once, HandlerScope scope) {
        std::string resourceName = GetResourceContextWithFallback(isolate, manager, handler);
        if (resourceName.empty()) {
            std::string methodName = std::string("Events.") + EventApiName(scope == HandlerScope::Client, once ? "once" : "on");
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, methodName + ": must be called from within a resource")));
            return;
        }

        EventHandler entry;
        entry.callback.Reset(isolate, handler);
        entry.resourceName = resourceName;
        entry.once         = once;

        std::scoped_lock lock(_handlersMutex);
        HandlerTable(scope)[std::string(eventName)].push_back(std::move(entry));
    }

    void Events::OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        OnImpl(args, HandlerScope::Global);
    }

    void Events::OnClientCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        OnImpl(args, HandlerScope::Client);
    }

    void Events::OnImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const char *api                = EventApiName(scope == HandlerScope::Client, "on");

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + " requires 2 arguments: eventName, handler")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + ": eventName must be a string")));
            return;
        }

        if (!args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + ": handler must be a function")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events || !ctx->resourceManager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string("Events.") + api + ": context not available")));
            return;
        }

        Events *events           = ctx->events;
        ResourceManager *manager = ctx->resourceManager;

        std::string eventName           = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();
        std::string resourceName        = GetResourceContextWithFallback(isolate, manager, handler);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string("Events.") + api + ": must be called from within a resource")));
            return;
        }

        events->RegisterHandler(isolate, manager, eventName, handler, false, scope);

        // Return unsubscribe function - use CallbackContext for lifetime safety
        v8::Local<v8::Object> data = v8::Object::New(isolate);
        data->Set(context, v8pp::to_v8(isolate, "eventName"), args[0]).Check();
        data->Set(context, v8pp::to_v8(isolate, "handler"), args[1]).Check();
        data->Set(context, v8pp::to_v8(isolate, "resourceName"), v8pp::to_v8(isolate, resourceName)).Check();
        data->Set(context, v8pp::to_v8(isolate, "context"), v8::External::New(isolate, ctx)).Check();
        data->Set(context, v8pp::to_v8(isolate, "scope"), v8::Integer::New(isolate, static_cast<int>(scope))).Check();

        v8::Local<v8::Function> unsubscribe = v8::Function::New(
            context,
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *iso                = info.GetIsolate();
                v8::Local<v8::Context> localCtx = iso->GetCurrentContext();
                v8::Local<v8::Object> d         = info.Data().As<v8::Object>();

                v8::Local<v8::Value> ctxVal  = d->Get(localCtx, v8pp::to_v8(iso, "context")).ToLocalChecked();
                CallbackContext *callbackCtx = static_cast<CallbackContext *>(ctxVal.As<v8::External>()->Value());

                // Check if the Events instance is still valid before accessing
                if (!callbackCtx || !callbackCtx->valid.load(std::memory_order_acquire)) {
                    return;
                }

                Events *eventsInst = callbackCtx->events;
                if (!eventsInst) {
                    return;
                }

                v8::Local<v8::Value> evtVal   = d->Get(localCtx, v8pp::to_v8(iso, "eventName")).ToLocalChecked();
                v8::Local<v8::Value> hndVal   = d->Get(localCtx, v8pp::to_v8(iso, "handler")).ToLocalChecked();
                v8::Local<v8::Value> resVal   = d->Get(localCtx, v8pp::to_v8(iso, "resourceName")).ToLocalChecked();
                v8::Local<v8::Value> scopeVal = d->Get(localCtx, v8pp::to_v8(iso, "scope")).ToLocalChecked();

                std::string evt             = v8pp::from_v8<std::string>(iso, evtVal);
                v8::Local<v8::Function> hnd = hndVal.As<v8::Function>();
                std::string resName         = v8pp::from_v8<std::string>(iso, resVal);
                HandlerScope scope          = static_cast<HandlerScope>(scopeVal->Int32Value(localCtx).FromMaybe(0));

                std::scoped_lock lock(eventsInst->_handlersMutex);
                auto &table = eventsInst->HandlerTable(scope);
                auto it     = table.find(evt);
                if (it != table.end()) {
                    auto &handlers = it->second;
                    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                       [&](const EventHandler &h) {
                                           return h.resourceName == resName && h.callback.Get(iso)->StrictEquals(hnd);
                                       }),
                        handlers.end());
                }
            },
            data)
                                                  .ToLocalChecked();

        args.GetReturnValue().Set(unsubscribe);
    }

    void Events::OnceCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        OnceImpl(args, HandlerScope::Global);
    }

    void Events::OnceClientCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        OnceImpl(args, HandlerScope::Client);
    }

    // Client -> server, dispatched there to onClient(name, (player, data)).
    void Events::EmitServerCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        BroadcastLuaEvent(args, "emitServer");
    }

    // Server -> every client, arriving as Core.Events.on(name, data).
    void Events::EmitAllClientsCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        BroadcastLuaEvent(args, "emitAllClients");
    }

    void Events::OnceImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        const char *api = EventApiName(scope == HandlerScope::Client, "once");

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + " requires 2 arguments: eventName, handler")));
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + ": invalid arguments")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events || !ctx->resourceManager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string("Events.") + api + ": context not available")));
            return;
        }

        std::string eventName           = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        ctx->events->RegisterHandler(isolate, ctx->resourceManager, eventName, handler, true, scope);
    }

    void Events::OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        OffImpl(args, HandlerScope::Global);
    }

    void Events::OffClientCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        OffImpl(args, HandlerScope::Client);
    }

    void Events::OffImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        const char *api = EventApiName(scope == HandlerScope::Client, "off");

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + " requires 2 arguments: eventName, handler")));
            return;
        }

        // Bad arguments must throw, like Events.on — a silent no-op here hid caller bugs.
        if (!args[0]->IsString() || !args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, std::string("Events.") + api + ": eventName must be a string and handler a function")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events || !ctx->resourceManager) {
            return;
        }

        Events *events           = ctx->events;
        ResourceManager *manager = ctx->resourceManager;

        std::string eventName           = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();
        std::string resourceName        = GetResourceContextWithFallback(isolate, manager, handler);

        // If no resource context, we can't determine which resource to remove handlers for
        if (resourceName.empty()) {
            return;
        }

        std::scoped_lock lock(events->_handlersMutex);
        auto &table = events->HandlerTable(scope);
        auto it     = table.find(eventName);
        if (it != table.end()) {
            auto &handlers = it->second;
            handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                               [&](const EventHandler &h) {
                                   return h.resourceName == resourceName && h.callback.Get(isolate)->StrictEquals(handler);
                               }),
                handlers.end());
        }
    }

    v8::Local<v8::Array> Events::InvokeHandlersToPromiseArray(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<std::pair<v8::Global<v8::Function>, std::string>> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName) {
        v8::Local<v8::Array> promises = v8::Array::New(isolate, static_cast<int>(handlers.size()));

        for (size_t i = 0; i < handlers.size(); ++i) {
            auto &[callback, logContext] = handlers[i];
            v8::Local<v8::Function> func = callback.Get(isolate);

            v8::TryCatch tryCatch(isolate);
            std::vector<v8::Local<v8::Value>> argv(args.begin(), args.end());

            v8::MaybeLocal<v8::Value> maybeResult = func->Call(context, context->Global(), static_cast<int>(argv.size()), argv.empty() ? nullptr : argv.data());

            if (tryCatch.HasCaught()) {
                // FormatV8Exception pulls the JS stack trace when available and
                // otherwise falls back to "<message>\n    at <file>:<line>:<col>",
                // so the script author can see where the throw actually happened
                // instead of just the bare error string.
                std::string errorStr = FormatV8Exception(isolate, tryCatch, "Unknown error in event handler");
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Event '{}' handler error: {}", logContext, eventName, errorStr);

                // Create rejected promise for this handler
                v8::Local<v8::Promise::Resolver> errResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                errResolver->Reject(context, tryCatch.Exception()).Check();
                promises->Set(context, static_cast<uint32_t>(i), errResolver->GetPromise()).Check();
                tryCatch.Reset();
            }
            else if (!maybeResult.IsEmpty()) {
                v8::Local<v8::Value> result = maybeResult.ToLocalChecked();

                // Wrap non-promise values in resolved promise
                if (result->IsPromise()) {
                    promises->Set(context, static_cast<uint32_t>(i), result).Check();
                }
                else {
                    v8::Local<v8::Promise::Resolver> valResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                    valResolver->Resolve(context, result).Check();
                    promises->Set(context, static_cast<uint32_t>(i), valResolver->GetPromise()).Check();
                }
            }
            else {
                // Empty result - create resolved promise with undefined
                v8::Local<v8::Promise::Resolver> valResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                valResolver->Resolve(context, v8::Undefined(isolate)).Check();
                promises->Set(context, static_cast<uint32_t>(i), valResolver->GetPromise()).Check();
            }
        }

        return promises;
    }

    void Events::RemovePendingCallback(AllSettledCallbackData *data) {
        std::scoped_lock lock(_pendingCallbacksMutex);
        for (auto it = _pendingCallbacks.begin(); it != _pendingCallbacks.end(); ++it) {
            if (it->get() == data) {
                _pendingCallbacks.erase(it);
                return;
            }
        }
    }

    void Events::AggregateWithAllSettled(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Array> promises, v8::Local<v8::Promise::Resolver> resolver, const std::string &aggregateErrorMessage) {
        // Get Promise.allSettled
        v8::MaybeLocal<v8::Value> maybePromiseCtor = context->Global()->Get(context, v8pp::to_v8(isolate, "Promise"));
        if (maybePromiseCtor.IsEmpty() || !maybePromiseCtor.ToLocalChecked()->IsObject()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return;
        }
        v8::Local<v8::Object> promiseConstructor = maybePromiseCtor.ToLocalChecked().As<v8::Object>();

        v8::MaybeLocal<v8::Value> maybeAllSettled = promiseConstructor->Get(context, v8pp::to_v8(isolate, "allSettled"));
        if (maybeAllSettled.IsEmpty() || !maybeAllSettled.ToLocalChecked()->IsFunction()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return;
        }
        v8::Local<v8::Function> allSettled = maybeAllSettled.ToLocalChecked().As<v8::Function>();

        v8::Local<v8::Value> allSettledArgs[]      = {promises};
        v8::MaybeLocal<v8::Value> allSettledResult = allSettled->Call(context, promiseConstructor, 1, allSettledArgs);

        if (allSettledResult.IsEmpty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return;
        }

        // A script-replaced Promise.allSettled may return a non-Promise; guard before
        // casting so we don't reach ->Then() on a bad handle. Soft-resolve like the siblings.
        v8::Local<v8::Value> allSettledValue = allSettledResult.ToLocalChecked();
        if (!allSettledValue->IsPromise()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return;
        }

        v8::Local<v8::Promise> allPromise = allSettledValue.As<v8::Promise>();

        // Allocate callback data and track it for cleanup on Events destruction
        // Use shared_ptr so the data survives until either:
        // - The then-handler runs and releases its reference, or
        // - Events is destroyed and releases its reference
        auto callbackData = std::make_shared<AllSettledCallbackData>();
        callbackData->resolver.Reset(isolate, resolver);
        callbackData->errorMessage = aggregateErrorMessage;
        callbackData->owner        = this;

        {
            std::scoped_lock lock(_pendingCallbacksMutex);
            _pendingCallbacks.insert(callbackData);
        }

        // Give the handler its own strong reference via the External, so ~Events() clearing
        // _pendingCallbacks can't free the data out from under a still-pending microtask.
        auto *handlerRef                     = new std::shared_ptr<AllSettledCallbackData>(callbackData);
        v8::Local<v8::External> resolverData = v8::External::New(isolate, handlerRef);

        v8::MaybeLocal<v8::Function> maybeThenHandler = v8::Function::New(
            context,
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *iso           = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();

                // Own the data for this call, then free the heap holder (a then-handler runs once).
                auto *handlerRef                             = static_cast<std::shared_ptr<AllSettledCallbackData> *>(info.Data().As<v8::External>()->Value());
                std::shared_ptr<AllSettledCallbackData> data = *handlerRef;
                delete handlerRef;

                if (data->cancelled.load(std::memory_order_acquire)) {
                    return; // Events destroyed, bail out safely
                }

                v8::Local<v8::Promise::Resolver> res = data->resolver.Get(iso);
                std::string errorMsg                 = data->errorMessage;

                std::vector<v8::Local<v8::Value>> rejections;

                // Records come from the script-mutable global Promise.allSettled, so the shape is
                // untrusted: validate each step and skip malformed records rather than aborting via
                // an unchecked cast or an empty ToLocalChecked().
                if (info.Length() > 0 && info[0]->IsArray()) {
                    v8::Local<v8::Array> results = info[0].As<v8::Array>();
                    for (uint32_t i = 0; i < results->Length(); ++i) {
                        // Contain a throwing getter so it skips the record instead of poisoning
                        // later MaybeLocals with a pending exception.
                        v8::TryCatch tryCatch(iso);

                        v8::Local<v8::Value> itemVal;
                        if (!results->Get(ctx, i).ToLocal(&itemVal) || !itemVal->IsObject()) {
                            continue;
                        }
                        v8::Local<v8::Object> item = itemVal.As<v8::Object>();

                        v8::Local<v8::Value> status;
                        if (!item->Get(ctx, v8pp::to_v8(iso, "status")).ToLocal(&status)) {
                            continue;
                        }
                        v8::String::Utf8Value statusStr(iso, status);
                        if (!*statusStr || std::string(*statusStr) != "rejected") {
                            continue;
                        }

                        v8::Local<v8::Value> reason;
                        if (item->Get(ctx, v8pp::to_v8(iso, "reason")).ToLocal(&reason)) {
                            rejections.push_back(reason);
                        }
                        else {
                            rejections.push_back(v8::Undefined(iso));
                        }
                    }
                }

                if (!rejections.empty()) {
                    // Create AggregateError
                    v8::Local<v8::Array> errArray = v8::Array::New(iso, static_cast<int>(rejections.size()));
                    for (size_t i = 0; i < rejections.size(); ++i) {
                        errArray->Set(ctx, static_cast<uint32_t>(i), rejections[i]).Check();
                    }

                    v8::Local<v8::String> msg                 = v8pp::to_v8(iso, errorMsg);
                    v8::Local<v8::Value> aggArgs[]            = {errArray, msg};
                    v8::MaybeLocal<v8::Value> aggErrorCtorVal = ctx->Global()->Get(ctx, v8pp::to_v8(iso, "AggregateError"));

                    if (!aggErrorCtorVal.IsEmpty() && aggErrorCtorVal.ToLocalChecked()->IsFunction()) {
                        v8::Local<v8::Function> aggErrorCtor   = aggErrorCtorVal.ToLocalChecked().As<v8::Function>();
                        v8::MaybeLocal<v8::Object> aggErrorObj = aggErrorCtor->NewInstance(ctx, 2, aggArgs);
                        if (!aggErrorObj.IsEmpty()) {
                            res->Reject(ctx, aggErrorObj.ToLocalChecked()).Check();
                        }
                        else {
                            res->Reject(ctx, errArray).Check();
                        }
                    }
                    else {
                        // Fallback if AggregateError not available
                        res->Reject(ctx, errArray).Check();
                    }
                }
                else {
                    res->Resolve(ctx, v8::Undefined(iso)).Check();
                }

                data->owner->RemovePendingCallback(data.get());
            },
            resolverData);

        // Function::New / Promise::Then return empty on a terminating isolate; ToLocalChecked()
        // would abort. Fail soft: the handler won't run, so free its holder and untrack here.
        v8::Local<v8::Function> thenHandler;
        if (!maybeThenHandler.ToLocal(&thenHandler)) {
            delete handlerRef;
            RemovePendingCallback(callbackData.get());
            return;
        }

        if (allPromise->Then(context, thenHandler).IsEmpty()) {
            delete handlerRef;
            RemovePendingCallback(callbackData.get());
            return;
        }
    }

    v8::Local<v8::Promise> Events::EmitInternal(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource, HandlerScope scope) {
        v8::EscapableHandleScope handleScope(isolate);

        // Create Promise resolver
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise            = resolver->GetPromise();

        // Collect handlers to call and track which ones to remove (once handlers)
        std::vector<std::pair<v8::Global<v8::Function>, std::string>> handlersToCall;
        std::vector<size_t> indicesToRemove;

        {
            std::scoped_lock lock(_handlersMutex);
            auto &table = HandlerTable(scope);
            auto it     = table.find(eventName);
            if (it != table.end()) {
                for (size_t idx = 0; idx < it->second.size(); ++idx) {
                    const auto &handler = it->second[idx];

                    // Filter by target resource if specified
                    if (!targetResource.empty() && handler.resourceName != targetResource) {
                        continue;
                    }

                    // Copy the callback for calling outside the lock
                    v8::Global<v8::Function> callbackCopy;
                    callbackCopy.Reset(isolate, handler.callback.Get(isolate));
                    handlersToCall.emplace_back(std::move(callbackCopy), handler.resourceName);

                    if (handler.once) {
                        indicesToRemove.push_back(idx);
                    }
                }

                // Remove once handlers (in reverse order to preserve indices)
                for (auto rit = indicesToRemove.rbegin(); rit != indicesToRemove.rend(); ++rit) {
                    it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(*rit));
                }
            }
        }

        if (handlersToCall.empty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return handleScope.Escape(promise);
        }

        // Invoke handlers and collect results as promises
        v8::Local<v8::Array> promises = InvokeHandlersToPromiseArray(isolate, context, handlersToCall, args, eventName);

        // Aggregate results using Promise.allSettled
        AggregateWithAllSettled(isolate, context, promises, resolver, "One or more event handlers failed");

        return handleScope.Escape(promise);
    }

    v8::Local<v8::Promise> Events::EmitReserved(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args, "", HandlerScope::Global);
    }

    v8::Local<v8::Promise> Events::EmitClient(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args, "", HandlerScope::Client);
    }

    void Events::EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Events.emit requires at least 1 argument: eventName")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Events.emit: eventName must be a string")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Events.emit: context not available")));
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);

        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 1; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        v8::Local<v8::Promise> promise = ctx->events->EmitInternal(isolate, context, eventName, eventArgs);
        args.GetReturnValue().Set(promise);
    }

    void Events::EmitToCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Events.emitTo requires at least 2 arguments: resourceName, eventName")));
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Events.emitTo: resourceName and eventName must be strings")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Events.emitTo: context not available")));
            return;
        }

        std::string resourceName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string eventName    = v8pp::from_v8<std::string>(isolate, args[1]);

        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 2; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        v8::Local<v8::Promise> promise = ctx->events->EmitInternal(isolate, context, eventName, eventArgs, resourceName);
        args.GetReturnValue().Set(promise);
    }

    void Events::OnLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Events.onLocal requires 2 arguments: eventName, handler")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events || !ctx->resourceManager) {
            return;
        }

        Events *events           = ctx->events;
        ResourceManager *manager = ctx->resourceManager;

        std::string eventName           = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();
        std::string resourceName        = GetResourceContextWithFallback(isolate, manager, handler);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Events.onLocal: must be called from within a resource")));
            return;
        }

        EventHandler entry;
        entry.callback.Reset(isolate, handler);
        entry.resourceName = resourceName;
        entry.once         = false;

        std::scoped_lock lock(events->_handlersMutex);
        events->_localHandlers[resourceName][eventName].push_back(std::move(entry));
    }

    void Events::EmitLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1 || !args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Events.emitLocal requires at least 1 argument: eventName")));
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events || !ctx->resourceManager) {
            return;
        }

        Events *events           = ctx->events;
        ResourceManager *manager = ctx->resourceManager;

        std::string eventName    = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string resourceName = GetResourceContextWithFallback(isolate, manager);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Events.emitLocal: must be called from within a resource")));
            return;
        }

        // Collect local handlers (convert to pairs with resource name as log context)
        std::vector<std::pair<v8::Global<v8::Function>, std::string>> handlersToCall;
        {
            std::scoped_lock lock(events->_handlersMutex);
            auto resIt = events->_localHandlers.find(resourceName);
            if (resIt != events->_localHandlers.end()) {
                auto evtIt = resIt->second.find(eventName);
                if (evtIt != resIt->second.end()) {
                    for (const auto &handler : evtIt->second) {
                        v8::Global<v8::Function> copy;
                        copy.Reset(isolate, handler.callback.Get(isolate));
                        handlersToCall.emplace_back(std::move(copy), resourceName);
                    }
                }
            }
        }

        // Create resolver for promise
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();

        if (handlersToCall.empty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            args.GetReturnValue().Set(resolver->GetPromise());
            return;
        }

        // Collect args
        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 1; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        // Invoke handlers and collect results as promises
        v8::Local<v8::Array> promises = InvokeHandlersToPromiseArray(isolate, context, handlersToCall, eventArgs, eventName);

        // Aggregate results using Promise.allSettled
        events->AggregateWithAllSettled(isolate, context, promises, resolver, "One or more local event handlers failed");

        args.GetReturnValue().Set(resolver->GetPromise());
    }

    void Events::CleanupResource(std::string_view resourceName) {
        std::scoped_lock lock(_handlersMutex);

        // Remove from global + client handlers
        for (auto *table : {&_globalHandlers, &_clientHandlers}) {
            for (auto &[eventName, handlers] : *table) {
                handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                   [&](const EventHandler &h) {
                                       return h.resourceName == resourceName;
                                   }),
                    handlers.end());
            }
        }

        // Remove local handlers entirely
        auto localIt = _localHandlers.find(resourceName);
        if (localIt != _localHandlers.end()) {
            _localHandlers.erase(localIt);
        }
    }

    void Events::ClearAll() {
        std::scoped_lock lock(_handlersMutex);

        // Explicitly Reset() all global + client handles before clearing to avoid
        // crash when destroying handlers from dead isolates
        for (auto *table : {&_globalHandlers, &_clientHandlers}) {
            for (auto &[eventName, handlers] : *table) {
                for (auto &handler : handlers) {
                    handler.callback.Reset();
                }
            }
            table->clear();
        }

        for (auto &[resourceName, eventMap] : _localHandlers) {
            for (auto &[eventName, handlers] : eventMap) {
                for (auto &handler : handlers) {
                    handler.callback.Reset();
                }
            }
        }
        _localHandlers.clear();
    }

    size_t Events::GetListenerCount(std::string_view eventName) {
        std::scoped_lock lock(_handlersMutex);
        auto it = _globalHandlers.find(eventName);
        if (it != _globalHandlers.end()) {
            return it->second.size();
        }
        return 0;
    }

    size_t Events::GetClientListenerCount(std::string_view eventName) {
        std::scoped_lock lock(_handlersMutex);
        auto it = _clientHandlers.find(eventName);
        if (it != _clientHandlers.end()) {
            return it->second.size();
        }
        return 0;
    }

    void Events::ListenerCountCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (args.Length() < 1 || !args[0]->IsString()) {
            args.GetReturnValue().Set(0);
            return;
        }

        CallbackContext *ctx = static_cast<CallbackContext *>(args.Data().As<v8::External>()->Value());
        if (!ctx || !ctx->events) {
            args.GetReturnValue().Set(0);
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        size_t count          = ctx->events->GetListenerCount(eventName);
        args.GetReturnValue().Set(static_cast<uint32_t>(count));
    }

} // namespace Framework::Scripting
