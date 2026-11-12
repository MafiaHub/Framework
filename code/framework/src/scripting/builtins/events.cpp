/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "events.h"
#include "../engine_helpers.h"
#include "../engine_scope.h"
#include "../function_references.h"
#include "../resource/resource_manager.h"
#include "../resource_monitor.h"
#include "../script_gate.h"
#include "../scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <utils/profiler.h>

#include <algorithm>

namespace Framework::Scripting::Builtins {

    struct Events::PendingEmission {
        struct Watched {
            size_t handler = 0;
            std::string resource;
        };
        v8::Global<v8::Promise::Resolver> resolver; // Empty for an emission nobody waits on.
        v8::Isolate *isolate     = nullptr;         // Where the resolver lives; its runtime may go before the emission settles.
        Events *owner            = nullptr;
        const char *errorMessage = "";
        std::string eventName;
        std::vector<Watched> watched; // The handlers whose promises it waits on, by the slot their reactions hold.
        std::vector<std::pair<size_t, v8::Global<v8::Value>>> failures;
        size_t unsettled = 1; // The dispatch itself counts, so a handler settling while later ones run cannot end it.
        std::atomic<bool> cancelled {false};

        // Nothing is aggregated any more: drop every handle into its isolate, which may be disposed before the handlers settle.
        void Cancel() {
            cancelled.store(true, std::memory_order_release);
            resolver.Reset();
            failures.clear();
        }
    };

    std::map<uint64_t, std::shared_ptr<Events::PendingEmission>> Events::_waitingEmissions;
    std::mutex Events::_waitingEmissionsMutex;
    uint64_t Events::_nextWaitingEmission = 1;

    size_t Events::GetWaitingEmissionCount() {
        std::scoped_lock lock(_waitingEmissionsMutex);
        return _waitingEmissions.size();
    }

    Events::~Events() {
        // Invalidate the callback context so any outstanding unsubscribe
        // lambdas holding a reference will safely bail out
        if (_callbackContext) {
            _callbackContext->valid.store(false, std::memory_order_release);
        }

        CancelPendingCallbacks();
    }

    void Events::CancelPendingCallbacks() {
        std::scoped_lock lock(_pendingCallbacksMutex);
        for (const auto &data : _pendingCallbacks) {
            data->Cancel();
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

        // Bridge, split by side: emitServer up (client), emitAllClients down (server).
        if (isClient) {
            v8::Local<v8::FunctionTemplate> emitServerTmpl = v8::FunctionTemplate::New(isolate, EmitServerCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "emitServer"), emitServerTmpl->GetFunction(context).ToLocalChecked()).Check();
        }
        else {
            v8::Local<v8::FunctionTemplate> emitAllTmpl = v8::FunctionTemplate::New(isolate, EmitAllClientsCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "emitAllClients"), emitAllTmpl->GetFunction(context).ToLocalChecked()).Check();

            // onClient/onceClient/offClient -- client-originated events (dispatched via EmitClient).
            // Backed by a table separate from on()/emit(), so a client can never target a native
            // handler. Server-only: only the server's instance ever dispatches into that table, so on
            // a client they would register handlers nothing can reach.
            v8::Local<v8::FunctionTemplate> onClientTmpl = v8::FunctionTemplate::New(isolate, OnClientCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "onClient"), onClientTmpl->GetFunction(context).ToLocalChecked()).Check();

            v8::Local<v8::FunctionTemplate> onceClientTmpl = v8::FunctionTemplate::New(isolate, OnceClientCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "onceClient"), onceClientTmpl->GetFunction(context).ToLocalChecked()).Check();

            v8::Local<v8::FunctionTemplate> offClientTmpl = v8::FunctionTemplate::New(isolate, OffClientCallback, contextData);
            eventsObj->Set(context, v8pp::to_v8(isolate, "offClient"), offClientTmpl->GetFunction(context).ToLocalChecked()).Check();
        }

        // Register as "Events" on target object
        target->Set(context, v8pp::to_v8(isolate, "Events"), eventsObj).Check();

        auto &metadata         = GetScriptingCatalog(isolate).global_object("Events", "Asynchronous resource event bus.");
        const auto handlerDocs = [](const char *returnType, const char *description, const char *returnDescription = "") {
            return v8pp::metadata::docs(returnType,
                {
                    v8pp::metadata::param("eventName", "string", false, "Case-sensitive event name."),
                    v8pp::metadata::param("handler", "EventHandler", false, "Resource-owned callback invoked with the emitted arguments."),
                },
                description, returnDescription);
        };
        // Instance::OnClientEvent hands a client event's server handlers the sending player, then the payload it carried.
        const auto clientHandlerDocs = [](const char *returnType, const char *description, const char *returnDescription = "") {
            return v8pp::metadata::docs(returnType,
                {
                    v8pp::metadata::param("eventName", "string", false, "Case-sensitive event name."),
                    v8pp::metadata::param("handler", "(player: Player, payload?: unknown) => unknown | Promise<unknown>", false,
                        "Resource-owned callback invoked with the sending player and the parsed payload, which is absent when the client sent none. The payload is untrusted."),
                },
                description, returnDescription);
        };
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("on", handlerDocs("Unsubscribe", "Registers a persistent handler in the shared event namespace.", "Function that removes this exact subscription.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("once", handlerDocs("void", "Registers a handler that is removed before its first invocation.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("off", handlerDocs("void", "Removes a matching handler owned by the calling resource.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emit",
            v8pp::metadata::docs("Promise<void>", {v8pp::metadata::param("eventName", "string", false, "Shared event name."), v8pp::metadata::rest_param("args", "unknown[]", "Arguments delivered to every matching handler.")},
                "Invokes every shared handler and waits for all synchronous and asynchronous results.", "Promise rejected with an AggregateError when one or more handlers fail.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitTo", v8pp::metadata::docs("Promise<void>",
                                                                                        {v8pp::metadata::param("resourceName", "string", false, "Destination running resource."), v8pp::metadata::param("eventName", "string", false, "Shared event name."),
                                                                                            v8pp::metadata::rest_param("args", "unknown[]", "Arguments delivered to matching handlers owned by the destination.")},
                                                                                        "Invokes matching handlers belonging only to one resource.", "Promise rejected when one or more destination handlers fail.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("onLocal", handlerDocs("void", "Registers a handler in the calling resource's private local-event namespace.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitLocal",
            v8pp::metadata::docs("Promise<void>", {v8pp::metadata::param("eventName", "string", false, "Private local event name."), v8pp::metadata::rest_param("args", "unknown[]", "Arguments delivered only to handlers owned by the calling resource.")},
                "Emits an event only within the calling resource.", "Promise rejected when one or more local handlers fail.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("listenerCount",
            v8pp::metadata::docs("number", {v8pp::metadata::param("eventName", "string", false, "Shared event name to inspect.")}, "Counts persistent and one-shot shared handlers across resources.", "Number of matching handlers.")));
        if (isClient) {
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitServer",
                v8pp::metadata::docs("void", {v8pp::metadata::param("eventName", "string", false, "Server-side client-event name."), v8pp::metadata::param("payload", "unknown", true, "Optional payload. A string is sent as is and must already be JSON text: the server parses it and drops an event that does not parse. Other values are JSON-serialized.")},
                    "Sends a named event from this client to the server's isolated onClient handlers.")));
        }
        else {
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("emitAllClients",
                v8pp::metadata::docs("void", {v8pp::metadata::param("eventName", "string", false, "Client shared-event name."), v8pp::metadata::param("payload", "unknown", true, "Optional payload. A string is sent as is and must already be JSON text: each client parses it and drops an event that does not parse. Other values are JSON-serialized.")},
                    "Broadcasts a named event to every connected client's shared Events handlers.")));
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("onClient",
                clientHandlerDocs("Unsubscribe", "Registers a persistent server handler for events originating from clients; this namespace is isolated from native events.", "Function that removes this exact subscription.")));
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("onceClient", clientHandlerDocs("void", "Registers a one-shot server handler for a client-originated event.")));
            metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("offClient", clientHandlerDocs("void", "Removes a matching client-originated event handler owned by the calling resource.")));
        }
    }

    // The three-tier resolution lives on ResourceManager, which owns all three tiers; this is the
    // null-manager guard its callers here need.
    std::string GetResourceContextWithFallback(v8::Isolate *isolate, ResourceManager *manager, v8::Local<v8::Function> handlerFn = {}) {
        return manager != nullptr ? manager->ResolveResourceContext(isolate, handlerFn) : std::string();
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
    // is sent as is, so it must already be JSON (receivers parse it); any other value is JSON-serialized
    // here. `api` names the caller in errors.
    static void BroadcastScriptEvent(const v8::FunctionCallbackInfo<v8::Value> &args, const char *api) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowTypeError(isolate, (std::string("Events.") + api + " requires an eventName string").c_str());
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
            // Missing subsystem is a state error, not a stale receiver — throw (README idiom).
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string("Events.") + api + ": network peer unavailable")));
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
        entry.isolate      = isolate;

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
            ThrowTypeError(isolate, (std::string("Events.") + api + " requires 2 arguments: eventName, handler").c_str());
            return;
        }

        if (!args[0]->IsString()) {
            ThrowTypeError(isolate, (std::string("Events.") + api + ": eventName must be a string").c_str());
            return;
        }

        if (!args[1]->IsFunction()) {
            ThrowTypeError(isolate, (std::string("Events.") + api + ": handler must be a function").c_str());
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
                    std::erase_if(handlers, [&](const EventHandler &h) {
                        return h.resourceName == resName && h.isolate == iso && h.callback.Get(iso)->StrictEquals(hnd);
                    });
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
        BroadcastScriptEvent(args, "emitServer");
    }

    // Server -> every client, arriving as Events.on(name, data).
    void Events::EmitAllClientsCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        BroadcastScriptEvent(args, "emitAllClients");
    }

    void Events::OnceImpl(const v8::FunctionCallbackInfo<v8::Value> &args, HandlerScope scope) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        const char *api = EventApiName(scope == HandlerScope::Client, "once");

        if (args.Length() < 2) {
            ThrowTypeError(isolate, (std::string("Events.") + api + " requires 2 arguments: eventName, handler").c_str());
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsFunction()) {
            ThrowTypeError(isolate, (std::string("Events.") + api + ": invalid arguments").c_str());
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
            ThrowTypeError(isolate, (std::string("Events.") + api + " requires 2 arguments: eventName, handler").c_str());
            return;
        }

        // Bad arguments must throw, like Events.on — a silent no-op here hid caller bugs.
        if (!args[0]->IsString() || !args[1]->IsFunction()) {
            ThrowTypeError(isolate, (std::string("Events.") + api + ": eventName must be a string and handler a function").c_str());
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

        // No resource context: like Events.on, this is a state error, not a silent no-op — we can't
        // determine which resource's handler to remove.
        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, std::string("Events.") + api + ": must be called from within a resource")));
            return;
        }

        std::scoped_lock lock(events->_handlersMutex);
        auto &table = events->HandlerTable(scope);
        auto it     = table.find(eventName);
        if (it != table.end()) {
            auto &handlers = it->second;
            std::erase_if(handlers, [&](const EventHandler &h) {
                return h.resourceName == resourceName && h.isolate == isolate && h.callback.Get(isolate)->StrictEquals(handler);
            });
        }
    }

    v8::MaybeLocal<v8::Value> Events::InvokeHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, CollectedHandler &handler, const std::vector<v8::Local<v8::Value>> &args, std::optional<std::vector<TransferredValue>> &copied, std::string_view eventName) {
        auto *monitor        = ResourceMonitor::Current();
        const double charged = monitor ? monitor->ChargedMs(handler.isolate, ResourceMonitor::Clock::now()) : 0.0;
        v8::MaybeLocal<v8::Value> result;
        if (handler.isolate == isolate) {
            ResourceMonitor::Scope scope(isolate);
            std::vector<v8::Local<v8::Value>> argv(args.begin(), args.end());
            result = handler.callback.Get(isolate)->Call(context, context->Global(), static_cast<int>(argv.size()), argv.empty() ? nullptr : argv.data());
        }
        else {
            // The handler's resource runs in a runtime of its own: call it there with the arguments copied across. What
            // it returns only matters as a veto or a promise to wait on, so its value is never copied back.
            FunctionReferences *references = (_callbackContext && _callbackContext->resourceManager) ? _callbackContext->resourceManager->GetFunctionReferences() : nullptr;
            if (references == nullptr) {
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "The handler's resource is not reachable from here")));
                return {};
            }
            if (!copied) {
                copied = references->CopyArguments(isolate, args);
                if (!copied) {
                    return {};
                }
            }
            result = references->Call(isolate, handler.isolate, handler.callback, *copied, FunctionReferences::Returned::Outcome);
        }
        // Its own time only: copying the arguments was the emitter's, and resources it called into are theirs.
        if (monitor) {
            monitor->ReportOwnTime(handler.isolate, "handler for", eventName, charged, ResourceMonitor::Clock::now());
        }
        return result;
    }

    void Events::DispatchHandlers(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<CollectedHandler> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName, v8::Local<v8::Promise::Resolver> resolver,
        const char *aggregateErrorMessage) {
        // Each failure is logged under its handler's resource; an emitter that does not await must not have it raised again as its own unhandled rejection.
        resolver->GetPromise()->MarkAsHandled();

        auto emission          = std::make_shared<PendingEmission>();
        emission->isolate      = isolate;
        emission->owner        = this;
        emission->errorMessage = aggregateErrorMessage;
        emission->resolver.Reset(isolate, resolver);
        uint64_t key = 0;
        std::optional<std::vector<TransferredValue>> copied;

        for (size_t i = 0; i < handlers.size(); ++i) {
            const std::string &logContext = handlers[i].resourceName;
            FW_PROFILE_SCOPE_N("Scripting::EventHandler");
            FW_PROFILE_TEXT(eventName.c_str(), eventName.size());
            FW_PROFILE_TEXT(logContext.c_str(), logContext.size());

            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> maybeResult = InvokeHandler(isolate, context, handlers[i], args, copied, eventName);

            if (tryCatch.HasCaught()) {
                // FormatV8Exception pulls the JS stack trace when available and otherwise falls back to
                // "<message>\n    at <file>:<line>:<col>", so the script author sees where the throw happened.
                std::string errorStr = FormatV8Exception(isolate, tryCatch, "Unknown error in event handler");
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Event '{}' handler error: {}", logContext, eventName, errorStr);
                emission->failures.emplace_back(i, v8::Global<v8::Value>(isolate, tryCatch.Exception()));
                tryCatch.Reset();
                continue;
            }

            v8::Local<v8::Value> result;
            if (maybeResult.ToLocal(&result) && result->IsPromise()) {
                WatchHandler(isolate, context, emission, key, i, logContext, eventName, result.As<v8::Promise>());
            }
        }

        ReleaseEmission(isolate, context, emission, key);
    }

    void Events::WatchHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<PendingEmission> &emission, uint64_t &key, size_t handler, const std::string &resourceName, const std::string &eventName, v8::Local<v8::Promise> promise) {
        if (key == 0) {
            emission->eventName = eventName;
            {
                std::scoped_lock lock(_pendingCallbacksMutex);
                _pendingCallbacks.insert(emission);
            }
            std::scoped_lock lock(_waitingEmissionsMutex);
            key = _nextWaitingEmission++;
            _waitingEmissions.emplace(key, emission);
        }

        // Both reactions hold the emission's key and the handler's slot.
        const uint64_t words[] = {key, emission->watched.size()};
        v8::Local<v8::BigInt> data;
        v8::Local<v8::Function> onFulfilled;
        v8::Local<v8::Function> onRejected;
        if (!v8::BigInt::NewFromWords(context, 0, 2, words).ToLocal(&data) || !v8::Function::New(context, OnHandlerFulfilled, data).ToLocal(&onFulfilled) || !v8::Function::New(context, OnHandlerRejected, data).ToLocal(&onRejected)
            || promise->Then(context, onFulfilled, onRejected).IsEmpty()) {
            return; // The isolate is terminating: nothing settles in it any more.
        }
        emission->watched.push_back({handler, resourceName});
        ++emission->unsettled;
    }

    void Events::OnHandlerFulfilled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        OnHandlerSettled(info, false);
    }

    void Events::OnHandlerRejected(const v8::FunctionCallbackInfo<v8::Value> &info) {
        OnHandlerSettled(info, true);
    }

    void Events::OnHandlerSettled(const v8::FunctionCallbackInfo<v8::Value> &info, bool rejected) {
        v8::Isolate *isolate = info.GetIsolate();
        uint64_t words[2]    = {0, 0};
        int signBit          = 0;
        int wordCount        = 2;
        info.Data().As<v8::BigInt>()->ToWordsArray(&signBit, &wordCount, words);

        // Gone when the emitter's runtime was disposed.
        std::shared_ptr<PendingEmission> emission;
        {
            std::scoped_lock lock(_waitingEmissionsMutex);
            const auto waiting = _waitingEmissions.find(words[0]);
            if (waiting == _waitingEmissions.end()) {
                return;
            }
            emission = waiting->second;
        }

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (rejected) {
            const auto &watched         = emission->watched[words[1]];
            v8::Local<v8::Value> reason = info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>();
            LogHandlerRejection(isolate, context, watched.resource, emission->eventName, reason);
            // Kept only for an emitter still waiting: a cancelled emission or one nobody waits on aggregates nothing.
            if (!emission->resolver.IsEmpty()) {
                emission->failures.emplace_back(watched.handler, v8::Global<v8::Value>(isolate, reason));
            }
        }
        ReleaseEmission(isolate, context, emission, words[0]);
    }

    void Events::ReleaseEmission(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<PendingEmission> &emission, uint64_t key) {
        if (--emission->unsettled != 0) {
            return;
        }
        if (key != 0) {
            {
                std::scoped_lock lock(_waitingEmissionsMutex);
                _waitingEmissions.erase(key);
            }
            if (emission->cancelled.load(std::memory_order_acquire)) {
                return; // Its Events is gone, or its caller stopped waiting.
            }
            emission->owner->RemovePendingCallback(emission.get());
        }
        if (emission->resolver.IsEmpty()) {
            return;
        }

        v8::Local<v8::Promise::Resolver> resolver = emission->resolver.Get(isolate);
        emission->resolver.Reset();
        if (emission->failures.empty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return;
        }

        std::ranges::sort(emission->failures, {}, &std::pair<size_t, v8::Global<v8::Value>>::first);
        v8::Local<v8::Array> errors = v8::Array::New(isolate, static_cast<int>(emission->failures.size()));
        for (size_t i = 0; i < emission->failures.size(); ++i) {
            errors->Set(context, static_cast<uint32_t>(i), emission->failures[i].second.Get(isolate)).Check();
        }

        v8::Local<v8::Value> aggregateArgs[] = {errors, v8pp::to_v8(isolate, emission->errorMessage)};
        v8::Local<v8::Value> aggregateCtor;
        v8::Local<v8::Object> aggregate;
        if (context->Global()->Get(context, v8pp::to_v8(isolate, "AggregateError")).ToLocal(&aggregateCtor) && aggregateCtor->IsFunction() && aggregateCtor.As<v8::Function>()->NewInstance(context, 2, aggregateArgs).ToLocal(&aggregate)) {
            resolver->Reject(context, aggregate).Check();
        }
        else {
            resolver->Reject(context, errors).Check();
        }
    }

    void Events::LogHandlerRejection(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &resourceName, const std::string &eventName, v8::Local<v8::Value> reason) {
        // A reason's stack getter or toString can throw; contain it here.
        v8::TryCatch tryCatch(isolate);

        // As the runtime's own unhandled-rejection report reads it: the stack of an error, else the value as text.
        std::string text;
        v8::Local<v8::Value> stack;
        if (reason->IsObject() && reason.As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "stack")).ToLocal(&stack) && stack->IsString()) {
            text = ToUtf8(isolate, stack);
        }
        if (text.empty()) {
            text = ToUtf8(isolate, reason);
        }
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Event '{}' handler error: {}", resourceName, eventName, text.empty() ? "Unknown error in event handler" : text);
    }

    void Events::RemovePendingCallback(PendingEmission *data) {
        std::scoped_lock lock(_pendingCallbacksMutex);
        for (auto it = _pendingCallbacks.begin(); it != _pendingCallbacks.end(); ++it) {
            if (it->get() == data) {
                _pendingCallbacks.erase(it);
                return;
            }
        }
    }

    void Events::CancelPendingEmission(v8::Isolate *isolate, v8::Local<v8::Promise> promise) {
        if (!isolate || promise.IsEmpty()) {
            return;
        }

        std::scoped_lock lock(_pendingCallbacksMutex);
        for (auto it = _pendingCallbacks.begin(); it != _pendingCallbacks.end(); ++it) {
            // Emissions from every resource's runtime wait here; only this isolate's resolvers can be read in it.
            const auto &data = *it;
            if (data->resolver.IsEmpty() || data->isolate != isolate) {
                continue;
            }

            auto resolver = data->resolver.Get(isolate);
            if (!resolver.IsEmpty() && resolver->GetPromise()->StrictEquals(promise)) {
                data->Cancel();
                _pendingCallbacks.erase(it);
                return;
            }
        }
    }

    std::vector<CollectedHandler> Events::CollectHandlers(const std::string &eventName, const std::string &targetResource, HandlerScope scope) {
        std::vector<CollectedHandler> handlersToCall;
        std::vector<size_t> indicesToRemove;

        std::scoped_lock lock(_handlersMutex);
        auto &table = HandlerTable(scope);
        auto it     = table.find(eventName);
        if (it == table.end()) {
            return handlersToCall;
        }

        for (size_t idx = 0; idx < it->second.size(); ++idx) {
            const auto &handler = it->second[idx];

            // Filter by target resource if specified
            if (!targetResource.empty() && handler.resourceName != targetResource) {
                continue;
            }

            // Copy the callback for calling outside the lock, in the isolate it belongs to
            handlersToCall.emplace_back(handler);

            if (handler.once) {
                indicesToRemove.push_back(idx);
            }
        }

        // Remove once handlers (in reverse order to preserve indices)
        for (auto rit = indicesToRemove.rbegin(); rit != indicesToRemove.rend(); ++rit) {
            it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(*rit));
        }

        return handlersToCall;
    }

    namespace {
        // Whether this thread is not the one scripts run on, on a host that gives them one.
        bool OffScriptThread() {
            ScriptGate *gate = ScriptGate::Get();
            return gate != nullptr && !gate->OnScriptThread();
        }
    } // namespace

    bool Events::EmitFromOtherThread(v8::Isolate *isolate, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource, HandlerScope scope, bool sync) {
        ResourceManager *manager       = _callbackContext ? _callbackContext->resourceManager : nullptr;
        FunctionReferences *references = manager != nullptr ? manager->GetFunctionReferences() : nullptr;
        Engine *engine                 = manager != nullptr ? manager->GetEngine() : nullptr;
        if (references == nullptr || engine == nullptr) {
            return true;
        }
        // Out of the caller's isolate, into a form that belongs to no isolate. An argument that cannot cross (a
        // promise, a cycle) is the caller's mistake: said once, and the event is not raised.
        v8::TryCatch tryCatch(isolate);
        std::optional<std::vector<TransferredValue>> copied = references->CopyArguments(isolate, args);
        if (!copied) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Event '{}' was not raised: {}", eventName, FormatV8Exception(isolate, tryCatch, "its arguments cannot be copied to the script thread"));
            tryCatch.Reset();
            return true;
        }

        // On the script thread, from its own isolate: what every emit there always did.
        auto emit = [this, engine, references, eventName, targetResource, scope, sync, arguments = std::move(*copied)](bool *result) {
            if (!engine->IsInitialized()) {
                return;
            }
            EngineScope entered(engine);
            v8::Isolate *host = entered.Isolate();
            v8::TryCatch failed(host);
            const auto rebuilt = references->RebuildArguments(host, arguments);
            if (!rebuilt) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Event '{}' was not raised: {}", eventName, FormatV8Exception(host, failed, "its arguments could not be rebuilt"));
                return;
            }
            if (sync) {
                // Only the global table has a synchronous form, and only the untargeted one.
                const bool ok = EmitReservedSync(host, entered.Context(), eventName, *rebuilt);
                if (result != nullptr) {
                    *result = ok;
                }
            }
            else {
                EmitInternal(host, entered.Context(), eventName, *rebuilt, targetResource, scope);
            }
        };

        ScriptGate *gate = ScriptGate::Get();
        if (sync) {
            // The caller keeps its isolate: the script thread never enters that one.
            bool proceed = true;
            gate->Run([&emit, &proceed] {
                emit(&proceed);
            });
            return proceed;
        }
        gate->Post([emit = std::move(emit)]() mutable {
            emit(nullptr);
        });
        return true;
    }

    v8::Local<v8::Promise> Events::EmitInternal(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource, HandlerScope scope) {
        v8::EscapableHandleScope handleScope(isolate);

        // Create Promise resolver
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise            = resolver->GetPromise();

        // Raised from a thread scripts do not run on: the handlers run on the script thread, later. The caller
        // gets a promise that is already settled, since nothing of the handlers comes back to this thread.
        if (OffScriptThread()) {
            EmitFromOtherThread(isolate, eventName, args, targetResource, scope, false);
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return handleScope.Escape(promise);
        }

        auto handlersToCall = CollectHandlers(eventName, targetResource, scope);

        if (handlersToCall.empty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return handleScope.Escape(promise);
        }

        DispatchHandlers(isolate, context, handlersToCall, args, eventName, resolver, "One or more event handlers failed");

        return handleScope.Escape(promise);
    }

    v8::Local<v8::Promise> Events::EmitReserved(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args, "", HandlerScope::Global);
    }

    bool Events::EmitReservedSync(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        v8::HandleScope handleScope(isolate);

        // From a thread scripts do not run on: the handlers run on the script thread and this waits for their
        // answer, which means for whatever script is running there first.
        if (OffScriptThread()) {
            return EmitFromOtherThread(isolate, eventName, args, "", HandlerScope::Global, true);
        }

        auto handlersToCall = CollectHandlers(eventName, "", HandlerScope::Global);
        bool proceed        = true;
        std::optional<std::vector<TransferredValue>> copied;

        // Nothing waits on an async handler here, so the emission has no resolver: it only logs their failures.
        auto emission     = std::make_shared<PendingEmission>();
        emission->isolate = isolate;
        emission->owner   = this;
        uint64_t key      = 0;

        for (size_t i = 0; i < handlersToCall.size(); ++i) {
            auto &handler                 = handlersToCall[i];
            const std::string &logContext = handler.resourceName;
            FW_PROFILE_SCOPE_N("Scripting::EventHandler");
            FW_PROFILE_TEXT(eventName.c_str(), eventName.size());
            FW_PROFILE_TEXT(logContext.c_str(), logContext.size());
            v8::TryCatch tryCatch(isolate);

            v8::MaybeLocal<v8::Value> maybeResult = InvokeHandler(isolate, context, handler, args, copied, eventName);

            if (tryCatch.HasCaught()) {
                std::string errorStr = FormatV8Exception(isolate, tryCatch, "Unknown error in event handler");
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Event '{}' handler error: {}", logContext, eventName, errorStr);
                tryCatch.Reset();
                continue;
            }

            // Literal false only, so a handler that forgets to return can't silently veto.
            v8::Local<v8::Value> result;
            if (maybeResult.ToLocal(&result) && result->IsBoolean() && !result->IsTrue()) {
                proceed = false;
            }
            else if (!result.IsEmpty() && result->IsPromise()) {
                WatchHandler(isolate, context, emission, key, i, logContext, eventName, result.As<v8::Promise>());
            }
        }

        ReleaseEmission(isolate, context, emission, key);
        return proceed;
    }

    v8::Local<v8::Promise> Events::EmitReservedTo(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &resourceName) {
        return EmitInternal(isolate, context, eventName, args, resourceName, HandlerScope::Global);
    }

    v8::Local<v8::Promise> Events::EmitClient(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args, "", HandlerScope::Client);
    }

    void Events::EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1) {
            ThrowTypeError(isolate, "Events.emit requires at least 1 argument: eventName");
            return;
        }

        if (!args[0]->IsString()) {
            ThrowTypeError(isolate, "Events.emit: eventName must be a string");
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
            ThrowTypeError(isolate, "Events.emitTo requires at least 2 arguments: resourceName, eventName");
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsString()) {
            ThrowTypeError(isolate, "Events.emitTo: resourceName and eventName must be strings");
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
            ThrowTypeError(isolate, "Events.onLocal requires 2 arguments: eventName, handler");
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
        entry.isolate      = isolate;

        std::scoped_lock lock(events->_handlersMutex);
        events->_localHandlers[resourceName][eventName].push_back(std::move(entry));
    }

    void Events::EmitLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowTypeError(isolate, "Events.emitLocal requires at least 1 argument: eventName");
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
        std::vector<CollectedHandler> handlersToCall;
        {
            std::scoped_lock lock(events->_handlersMutex);
            auto resIt = events->_localHandlers.find(resourceName);
            if (resIt != events->_localHandlers.end()) {
                auto evtIt = resIt->second.find(eventName);
                if (evtIt != resIt->second.end()) {
                    for (const auto &handler : evtIt->second) {
                        handlersToCall.emplace_back(handler);
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

        events->DispatchHandlers(isolate, context, handlersToCall, eventArgs, eventName, resolver, "One or more local event handlers failed");

        args.GetReturnValue().Set(resolver->GetPromise());
    }

    void Events::CleanupResource(std::string_view resourceName) {
        std::scoped_lock lock(_handlersMutex);

        // Remove from global + client handlers
        for (auto *table : {&_globalHandlers, &_clientHandlers}) {
            for (auto &[eventName, handlers] : *table) {
                std::erase_if(handlers, [&](const EventHandler &h) {
                    return h.resourceName == resourceName;
                });
            }
        }

        // Remove local handlers entirely
        auto localIt = _localHandlers.find(resourceName);
        if (localIt != _localHandlers.end()) {
            _localHandlers.erase(localIt);
        }
    }

    void Events::ForgetIsolate(v8::Isolate *isolate) {
        {
            std::scoped_lock lock(_handlersMutex);
            for (auto *table : {&_globalHandlers, &_clientHandlers}) {
                for (auto &[eventName, handlers] : *table) {
                    std::erase_if(handlers, [&](const EventHandler &h) {
                        return h.isolate == isolate;
                    });
                }
            }
            for (auto &[resourceName, eventMap] : _localHandlers) {
                for (auto &[eventName, handlers] : eventMap) {
                    std::erase_if(handlers, [&](const EventHandler &h) {
                        return h.isolate == isolate;
                    });
                }
            }
        }

        // An emission from this isolate still waiting on its handlers never settles now: its then-handler dies with
        // the isolate, so the data it would have taken is freed here.
        {
            std::scoped_lock lock(_pendingCallbacksMutex);
            for (auto it = _pendingCallbacks.begin(); it != _pendingCallbacks.end();) {
                if ((*it)->isolate == isolate) {
                    (*it)->Cancel();
                    it = _pendingCallbacks.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
        std::scoped_lock lock(_waitingEmissionsMutex);
        std::erase_if(_waitingEmissions, [&](const auto &entry) {
            return entry.second->isolate == isolate;
        });
    }

    void Events::Reset() {
        ClearAll();
        CancelPendingCallbacks();
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

} // namespace Framework::Scripting::Builtins
