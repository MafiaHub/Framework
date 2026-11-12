/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "events.h"
#include "../bind/v8_backend.h"
#include "../bind/v8_callable.h"
#include "../engine_helpers.h"
#include "../function_references.h"
#include "../lua/lua_runtime.h"
#include "../resource/resource_manager.h"
#include "../resource_monitor.h"
#include "../script_log.h"
#include "../scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <utils/profiler.h>

#include <algorithm>
#include <unordered_map>

namespace Framework::Scripting::Builtins {

    // Context for AllSettled callback - holds resolver, error message, and owner for cleanup
    // Defined early so destructor can access it
    struct Events::AllSettledCallbackData {
        v8::Global<v8::Promise::Resolver> resolver;
        v8::Isolate *isolate = nullptr; // Where the resolver lives; its runtime may go before the emission settles.
        std::string errorMessage;
        Events *owner;                       // For removing from pending set on completion
        std::atomic<bool> cancelled {false}; // Set when Events is destroyed
    };

    std::map<uint64_t, std::shared_ptr<Events::AllSettledCallbackData>> Events::_waitingEmissions;
    std::mutex Events::_waitingEmissionsMutex;
    uint64_t Events::_nextWaitingEmission = 1;

    size_t Events::GetWaitingEmissionCount() {
        std::scoped_lock lock(_waitingEmissionsMutex);
        return _waitingEmissions.size();
    }

    namespace {
        // What Register bound an isolate to: the functions of the definition carry nothing of their own, so a
        // JavaScript call finds its Events by the isolate it runs in. A Lua call asks the manager of its runtime.
        struct Binding final {
            Events *events           = nullptr;
            ResourceManager *manager = nullptr;
        };

        std::mutex &BindingsMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<v8::Isolate *, Binding> &Bindings() {
            static std::unordered_map<v8::Isolate *, Binding> bindings;
            return bindings;
        }

        // The Events a call works on and the manager it was registered with. A JavaScript call in an isolate
        // nobody registered, and every Lua call, is answered by the manager of its runtime.
        Binding BindingOf(const Bind::Call &call) {
            if (call.GetLanguage() == Language::JavaScript) {
                std::scoped_lock lock(BindingsMutex());
                const auto found = Bindings().find(v8::Isolate::GetCurrent());
                if (found != Bindings().end()) {
                    return found->second;
                }
            }
            ResourceManager *manager = call.GetResourceManager();
            return {manager != nullptr ? &manager->GetEvents() : nullptr, manager};
        }

        // `manager` is the one the handler's resource belongs to, as the key of its share of the log.
        void LogHandlerError(const void *manager, const std::string &resourceName, const std::string &eventName, const std::string &error) {
            LogScriptError(manager, resourceName, "Event '" + BlankedLine(eventName) + "' handler error: " + error);
        }

        // What a handler that was called through its callable failed with, for the emitter. A handler whose runtime
        // went while the emission waited on it reads as it does between two JavaScript runtimes.
        std::string FailureOf(const Bind::CallOutcome &outcome, const std::string &resourceName) {
            return outcome.error == Bind::kRuntimeStopped ? resourceName + " stopped before the call finished" : outcome.error;
        }

        // The isolate a callable is charged to by the resource monitor, which follows isolates: none for Lua.
        v8::Isolate *MonitoredIsolate(const Bind::ScriptCallable &callable) {
            return callable.GetLanguage() == Language::JavaScript ? static_cast<const Bind::V8Callable &>(callable).Isolate() : nullptr;
        }

        // Why an emission could not copy one of its arguments: `index` counts the event arguments from zero, and
        // the text from one.
        std::string ArgumentError(std::string_view api, size_t index, const std::string &why) {
            return std::string(api) + ": argument " + std::to_string(index + 1) + ": " + why;
        }

        constexpr const char *kHandlersFailed      = "One or more event handlers failed";
        constexpr const char *kLocalHandlersFailed = "One or more local event handlers failed";
    } // namespace

    struct Events::CallableArguments final {
        bool copied = false;
        std::vector<TransferredValue> values;
        std::string error; // why they cannot cross; empty when they can

        // Copies `args` out of the emitter's isolate the first time it is asked. False when they cannot cross,
        // which is logged that first time, once for the emission, under the resource of the handler that asked.
        bool Ensure(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<v8::Local<v8::Value>> &args, std::string_view api, const void *manager, const std::string &resourceName, const std::string &eventName) {
            if (!copied) {
                copied = true;
                values.reserve(args.size());
                for (size_t i = 0; i < args.size(); ++i) {
                    auto value = ValueTransfer::Copy(isolate, context, args[i], Bind::V8CallableTransfer());
                    if (!value) {
                        error = ArgumentError(api, i, value.GetError());
                        values.clear();
                        LogHandlerError(manager, resourceName, eventName, error);
                        break;
                    }
                    values.push_back(std::move(value).GetValue());
                }
            }
            return error.empty();
        }
    };

    Events::~Events() {
        // An unsubscribe function a script still holds finds nothing from here on.
        *_self = nullptr;
        {
            std::scoped_lock lock(BindingsMutex());
            std::erase_if(Bindings(), [this](const auto &binding) {
                return binding.second.events == this;
            });
        }

        CancelPendingCallbacks();
    }

    void Events::CancelPendingCallbacks() {
        std::scoped_lock lock(_pendingCallbacksMutex);
        for (const auto &data : _pendingCallbacks) {
            data->cancelled.store(true, std::memory_order_release);
            data->resolver.Reset();
        }
        _pendingCallbacks.clear();
    }

    // Lets go of the handlers whose runtime has stopped, which are never called again, and counts the others. A
    // Lua runtime that went without its resource being stopped through the manager leaves such handlers behind.
    static size_t LiveHandlers(std::vector<EventHandler> &handlers) {
        std::erase_if(handlers, [](const EventHandler &h) {
            return !h.callable->IsAlive();
        });
        return handlers.size();
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

    // The functions of the definition: one body for both languages. A JavaScript emit alone speaks V8, to hand
    // its arguments to the JavaScript handlers as they are and to return the Promise it always returned.
    struct Events::Script final {
        // The entry of the handler in argument 1, owned by `resourceName`.
        static EventHandler MakeHandler(Bind::Call &call, std::string resourceName, bool once) {
            EventHandler entry;
            entry.callable     = call.Arg(1).ToCallable();
            entry.resourceName = std::move(resourceName);
            entry.once         = once;
            if (call.GetLanguage() == Language::JavaScript) {
                entry.isolate  = v8::Isolate::GetCurrent();
                entry.callback = std::make_shared<v8::Global<v8::Function>>(entry.isolate, Bind::V8ValueOf(call, call.Arg(1)).As<v8::Function>());
            }
            return entry;
        }

        // Removes the handlers of `resourceName` for the event that are the function of `callable`.
        static void Remove(Events &events, HandlerScope scope, const std::string &eventName, const std::string &resourceName, const Bind::ScriptCallable &callable) {
            std::scoped_lock lock(events._handlersMutex);
            auto &table = events.HandlerTable(scope);
            auto it     = table.find(eventName);
            if (it != table.end()) {
                std::erase_if(it->second, [&](const EventHandler &h) {
                    return h.resourceName == resourceName && h.callable->Equals(callable);
                });
            }
        }

        static void On(Bind::Call &call, HandlerScope scope) {
            const std::string api = std::string("Events.") + EventApiName(scope == HandlerScope::Client, "on");

            if (call.ArgCount() < 2) {
                call.ThrowTypeError(api + " requires 2 arguments: eventName, handler");
                return;
            }
            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError(api + ": eventName must be a string");
                return;
            }
            if (!call.Arg(1).IsFunction()) {
                call.ThrowTypeError(api + ": handler must be a function");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr || binding.manager == nullptr) {
                call.ThrowError(api + ": context not available");
                return;
            }

            const std::string eventName    = call.Arg(0).AsString();
            const std::string resourceName = call.OwnerOf(call.Arg(1));
            if (resourceName.empty()) {
                call.ThrowError(api + ": must be called from within a resource");
                return;
            }

            EventHandler entry = MakeHandler(call, resourceName, false);
            {
                std::scoped_lock lock(binding.events->_handlersMutex);
                binding.events->HandlerTable(scope)[eventName].push_back(std::move(entry));
            }

            // The unsubscribe function removes what `off` would, given the same handler, whenever it is called: also
            // a registration made after an earlier removal. It carries the handler as a script value, which the
            // collector of the language sees, and asks for its callable when it is called. Holding the callable
            // would root the handler: one that holds its own unsubscribe function would then hold itself, and never
            // be let go of once it is removed.
            call.Return(call.NewFunction(
                [self = binding.events->_self, scope, eventName, resourceName](Bind::Call &unsubscribe) {
                    Events *events                                       = *self;
                    const std::shared_ptr<Bind::ScriptCallable> callable = unsubscribe.Data().ToCallable();
                    if (events != nullptr && callable != nullptr) {
                        Remove(*events, scope, eventName, resourceName, *callable);
                    }
                },
                call.Arg(1)));
        }

        static void Once(Bind::Call &call, HandlerScope scope) {
            const std::string api = std::string("Events.") + EventApiName(scope == HandlerScope::Client, "once");

            if (call.ArgCount() < 2) {
                call.ThrowTypeError(api + " requires 2 arguments: eventName, handler");
                return;
            }
            if (!call.Arg(0).IsString() || !call.Arg(1).IsFunction()) {
                call.ThrowTypeError(api + ": invalid arguments");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr || binding.manager == nullptr) {
                call.ThrowError(api + ": context not available");
                return;
            }

            std::string resourceName = call.OwnerOf(call.Arg(1));
            if (resourceName.empty()) {
                call.ThrowError(api + ": must be called from within a resource");
                return;
            }

            EventHandler entry = MakeHandler(call, std::move(resourceName), true);
            std::scoped_lock lock(binding.events->_handlersMutex);
            binding.events->HandlerTable(scope)[call.Arg(0).AsString()].push_back(std::move(entry));
        }

        static void Off(Bind::Call &call, HandlerScope scope) {
            const std::string api = std::string("Events.") + EventApiName(scope == HandlerScope::Client, "off");

            if (call.ArgCount() < 2) {
                call.ThrowTypeError(api + " requires 2 arguments: eventName, handler");
                return;
            }
            // Bad arguments must throw, like Events.on — a silent no-op here hid caller bugs.
            if (!call.Arg(0).IsString() || !call.Arg(1).IsFunction()) {
                call.ThrowTypeError(api + ": eventName must be a string and handler a function");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr || binding.manager == nullptr) {
                call.ThrowError(api + ": context not available");
                return;
            }

            // No resource context: like Events.on, this is a state error, not a silent no-op — we can't
            // determine which resource's handler to remove.
            const std::string resourceName = call.OwnerOf(call.Arg(1));
            if (resourceName.empty()) {
                call.ThrowError(api + ": must be called from within a resource");
                return;
            }

            const std::shared_ptr<Bind::ScriptCallable> callable = call.Arg(1).ToCallable();
            Remove(*binding.events, scope, call.Arg(0).AsString(), resourceName, *callable);
        }

        // Picks the handlers of one emission, which takes the once handlers out of their table.
        using Collector = fu2::function_view<std::vector<CollectedHandler>()>;

        // Emits to what `collect` picks, with the arguments of the call from `first` on. JavaScript gets the
        // Promise of the emission. Lua waits for it: the call returns nothing once every handler has settled, or
        // raises `failureMessage` and what the handlers failed with.
        static void EmitTo(Bind::Call &call, Events &events, Collector collect, const std::string &eventName, size_t first, const char *api, const char *failureMessage) {
            if (call.GetLanguage() == Language::JavaScript) {
                v8::Isolate *isolate           = v8::Isolate::GetCurrent();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                std::vector<v8::Local<v8::Value>> args;
                for (size_t i = first; i < call.ArgCount(); ++i) {
                    args.push_back(Bind::V8ValueOf(call, call.Arg(i)));
                }
                std::vector<CollectedHandler> handlers = collect();
                call.Return(Bind::V8ValueIn(call, events.EmitToHandlers(isolate, context, handlers, args, eventName, failureMessage, api)));
                return;
            }

            // Copied once, whoever listens, and before the handlers are picked: what cannot cross is the emitter's
            // mistake, and an emit that fails on it has called nobody and used up no once handler.
            std::vector<TransferredValue> args;
            args.reserve(call.ArgCount() - first);
            for (size_t i = first; i < call.ArgCount(); ++i) {
                auto copied = call.Arg(i).ToTransfer();
                if (!copied) {
                    call.ThrowTypeError(ArgumentError(api, i - first, copied.GetError()));
                    return;
                }
                args.push_back(std::move(copied).GetValue());
            }

            Bind::Deferred deferred = call.Defer();
            EmitCopied(events._resourceManager, collect(), args, eventName, [deferred, failureMessage](std::vector<std::string> &&errors) mutable {
                if (errors.empty()) {
                    deferred.Resolve();
                }
                else {
                    deferred.Reject(DescribeFailures(failureMessage, errors));
                }
            });
        }

        static void Emit(Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowTypeError("Events.emit requires at least 1 argument: eventName");
                return;
            }
            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Events.emit: eventName must be a string");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr) {
                call.ThrowError("Events.emit: context not available");
                return;
            }

            const std::string eventName = call.Arg(0).AsString();
            EmitTo(
                call, *binding.events,
                [&] {
                    return binding.events->CollectHandlers(eventName, "", HandlerScope::Global);
                },
                eventName, 1, "Events.emit", kHandlersFailed);
        }

        static void EmitToResource(Bind::Call &call) {
            if (call.ArgCount() < 2) {
                call.ThrowTypeError("Events.emitTo requires at least 2 arguments: resourceName, eventName");
                return;
            }
            if (!call.Arg(0).IsString() || !call.Arg(1).IsString()) {
                call.ThrowTypeError("Events.emitTo: resourceName and eventName must be strings");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr) {
                call.ThrowError("Events.emitTo: context not available");
                return;
            }

            const std::string resourceName = call.Arg(0).AsString();
            const std::string eventName    = call.Arg(1).AsString();
            // An empty name targets nobody in particular, as it always did: every handler of the event.
            EmitTo(
                call, *binding.events,
                [&] {
                    return binding.events->CollectHandlers(eventName, resourceName, HandlerScope::Global);
                },
                eventName, 2, "Events.emitTo", kHandlersFailed);
        }

        static void OnLocal(Bind::Call &call) {
            if (call.ArgCount() < 2 || !call.Arg(0).IsString() || !call.Arg(1).IsFunction()) {
                call.ThrowTypeError("Events.onLocal requires 2 arguments: eventName, handler");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr || binding.manager == nullptr) {
                return;
            }

            const std::string resourceName = call.OwnerOf(call.Arg(1));
            if (resourceName.empty()) {
                call.ThrowError("Events.onLocal: must be called from within a resource");
                return;
            }

            EventHandler entry = MakeHandler(call, resourceName, false);
            std::scoped_lock lock(binding.events->_handlersMutex);
            binding.events->_localHandlers[resourceName][call.Arg(0).AsString()].push_back(std::move(entry));
        }

        static void EmitLocal(Bind::Call &call) {
            if (call.ArgCount() < 1 || !call.Arg(0).IsString()) {
                call.ThrowTypeError("Events.emitLocal requires at least 1 argument: eventName");
                return;
            }

            const Binding binding = BindingOf(call);
            if (binding.events == nullptr || binding.manager == nullptr) {
                return;
            }

            const std::string resourceName = call.ResourceName();
            if (resourceName.empty()) {
                call.ThrowError("Events.emitLocal: must be called from within a resource");
                return;
            }

            const std::string eventName = call.Arg(0).AsString();
            EmitTo(
                call, *binding.events,
                [&] {
                    return binding.events->CollectLocalHandlers(resourceName, eventName);
                },
                eventName, 1, "Events.emitLocal", kLocalHandlersFailed);
        }

        static void ListenerCount(Bind::Call &call) {
            const Binding binding = BindingOf(call);
            if (call.ArgCount() < 1 || !call.Arg(0).IsString() || binding.events == nullptr) {
                call.Return(int64_t {0});
                return;
            }
            call.Return(static_cast<int64_t>(binding.events->GetListenerCount(call.Arg(0).AsString())));
        }

        // Shared body for emitServer/emitAllClients: BroadcastRPC a named event + payload over the peer's
        // connections (a client peer reaches only the server, a server peer all clients). A string payload
        // is sent as is, so it must already be JSON (receivers parse it); any other value is JSON-serialized
        // here. `api` names the caller in errors.
        static void Broadcast(Bind::Call &call, const char *api) {
            if (call.ArgCount() < 1 || !call.Arg(0).IsString()) {
                call.ThrowTypeError(std::string("Events.") + api + " requires an eventName string");
                return;
            }
            const std::string eventName = call.Arg(0).AsString();
            if (eventName.empty()) {
                return;
            }

            std::string payload;
            const Bind::Value value = call.Arg(1);
            if (!value.IsNil()) {
                if (value.IsString()) {
                    payload = value.AsString();
                }
                else {
                    auto json = value.ToJson();
                    if (!json) {
                        call.ThrowTypeError(std::string("Events.") + api + ": " + json.GetError());
                        return;
                    }
                    payload = std::move(json).GetValue();
                }
            }

            auto *peer = CoreModules::GetNetworkPeer();
            if (!peer) {
                // Missing subsystem is a state error, not a stale receiver — throw (README idiom).
                call.ThrowError(std::string("Events.") + api + ": network peer unavailable");
                return;
            }
            Framework::Integrations::Shared::RPC::EmitScriptEvent ev;
            ev.FromParameters(eventName, payload);
            peer->BroadcastRPC(ev);
        }

        // Declares the Events global.
        static Bind::Namespace Define();
    };

    Bind::Namespace Events::Script::Define() {
        const auto scoped = [](void (*function)(Bind::Call &, HandlerScope), HandlerScope scope) -> Bind::NativeFunction {
            return [function, scope](Bind::Call &call) {
                function(call, scope);
            };
        };
        const auto broadcast = [](const char *api) -> Bind::NativeFunction {
            return [api](Bind::Call &call) {
                Broadcast(call, api);
            };
        };

        const Bind::TypeRef text        = Bind::TypeOf<std::string>();
        const Bind::TypeRef unsubscribe = Framework::Scripting::Bind::Type::Custom("Unsubscribe", "fun()");
        const Bind::TypeRef promise     = Framework::Scripting::Bind::Type::Custom("Promise<void>", "nil");
        const Bind::TypeRef rest        = Framework::Scripting::Bind::Type::Custom("unknown[]", "any");
        const Bind::TypeRef payload     = Framework::Scripting::Bind::Type::Custom("unknown", "any");

        const auto handlerDocs = [&text](const char *description) {
            return Framework::Scripting::Bind::Doc(description).Param("eventName", text, "Case-sensitive event name.").Param("handler", Framework::Scripting::Bind::Type::Custom("EventHandler", "fun(...: any): any"), "Resource-owned callback invoked with the emitted arguments.");
        };
        // Instance::OnClientEvent hands a client event's server handlers the sending player, then the payload it carried.
        const auto clientHandlerDocs = [&text](const char *description) {
            return Framework::Scripting::Bind::Doc(description)
                .Param("eventName", text, "Case-sensitive event name.")
                .Param("handler", Framework::Scripting::Bind::Type::Custom("(player: Player, payload?: unknown) => unknown | Promise<unknown>", "fun(player: Player, payload?: any): any"),
                    "Resource-owned callback invoked with the sending player and the parsed payload, which is absent when the client sent none. The payload is untrusted.");
        };

        return Bind::Namespace("Events", "Asynchronous resource event bus.")
            .Function("on", scoped(&Script::On, HandlerScope::Global), handlerDocs("Registers a persistent handler in the shared event namespace.").ReturnType(unsubscribe).Returns("Function that removes this exact subscription."))
            .Function("once", scoped(&Script::Once, HandlerScope::Global), handlerDocs("Registers a handler that is removed before its first invocation."))
            .Function("off", scoped(&Script::Off, HandlerScope::Global), handlerDocs("Removes a matching handler owned by the calling resource."))
            .Function("emit", &Script::Emit,
                Framework::Scripting::Bind::Doc("Invokes every shared handler and waits for all synchronous and asynchronous results.")
                    .Param("eventName", text, "Shared event name.")
                    .RestParam("args", rest, "Arguments delivered to every matching handler.")
                    .ReturnType(promise)
                    .Returns("Promise rejected with an AggregateError when one or more handlers fail."))
            .Function("emitTo", &Script::EmitToResource,
                Framework::Scripting::Bind::Doc("Invokes matching handlers belonging only to one resource.")
                    .Param("resourceName", text, "Destination running resource.")
                    .Param("eventName", text, "Shared event name.")
                    .RestParam("args", rest, "Arguments delivered to matching handlers owned by the destination.")
                    .ReturnType(promise)
                    .Returns("Promise rejected when one or more destination handlers fail."))
            .Function("onLocal", &Script::OnLocal, handlerDocs("Registers a handler in the calling resource's private local-event namespace."))
            .Function("emitLocal", &Script::EmitLocal,
                Framework::Scripting::Bind::Doc("Emits an event only within the calling resource.")
                    .Param("eventName", text, "Private local event name.")
                    .RestParam("args", rest, "Arguments delivered only to handlers owned by the calling resource.")
                    .ReturnType(promise)
                    .Returns("Promise rejected when one or more local handlers fail."))
            .Function("listenerCount", &Script::ListenerCount, Framework::Scripting::Bind::Doc("Counts persistent and one-shot shared handlers across resources.").Param("eventName", text, "Shared event name to inspect.").ReturnType(Bind::TypeOf<double>()).Returns("Number of matching handlers."))
            // Bridge, split by side: emitServer up (client), emitAllClients down (server).
            .Function("emitServer", broadcast("emitServer"),
                Framework::Scripting::Bind::Doc("Sends a named event from this client to the server's isolated onClient handlers.")
                    .Param("eventName", text, "Server-side client-event name.")
                    .OptionalParam("payload", payload, "Optional payload. A string is sent as is and must already be JSON text: the server parses it and drops an event that does not parse. Other values are JSON-serialized."),
                Framework::Scripting::Bind::Side::Client)
            .Function("emitAllClients", broadcast("emitAllClients"),
                Framework::Scripting::Bind::Doc("Broadcasts a named event to every connected client's shared Events handlers.")
                    .Param("eventName", text, "Client shared-event name.")
                    .OptionalParam("payload", payload, "Optional payload. A string is sent as is and must already be JSON text: each client parses it and drops an event that does not parse. Other values are JSON-serialized."),
                Framework::Scripting::Bind::Side::Server)
            // onClient/onceClient/offClient -- client-originated events (dispatched via EmitClient).
            // Backed by a table separate from on()/emit(), so a client can never target a native
            // handler. Server-only: only the server's instance ever dispatches into that table, so on
            // a client they would register handlers nothing can reach.
            .Function("onClient", scoped(&Script::On, HandlerScope::Client),
                clientHandlerDocs("Registers a persistent server handler for events originating from clients; this namespace is isolated from native events.").ReturnType(unsubscribe).Returns("Function that removes this exact subscription."), Framework::Scripting::Bind::Side::Server)
            .Function("onceClient", scoped(&Script::Once, HandlerScope::Client), clientHandlerDocs("Registers a one-shot server handler for a client-originated event."), Framework::Scripting::Bind::Side::Server)
            .Function("offClient", scoped(&Script::Off, HandlerScope::Client), clientHandlerDocs("Removes a matching client-originated event handler owned by the calling resource."), Framework::Scripting::Bind::Side::Server);
    }

    const Bind::Namespace &Events::Definition() {
        static const Bind::Namespace definition = Script::Define();
        return definition;
    }

    void Events::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager, bool isClient) {
        _resourceManager = resourceManager;
        {
            std::scoped_lock lock(BindingsMutex());
            Bindings()[isolate] = {this, resourceManager};
        }
        // The owner of a handler is asked of the manager of the runtime.
        if (resourceManager != nullptr) {
            Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, target, GetScriptingCatalog(isolate), isClient ? Bind::Side::Client : Bind::Side::Server);
    }

    void Events::RegisterLua(LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

    std::string Events::DescribeFailures(std::string_view message, const std::vector<std::string> &errors) {
        std::string described(message);
        for (size_t i = 0; i < errors.size(); ++i) {
            described += i == 0 ? ": " : "; ";
            described += errors[i];
        }
        return described;
    }

    v8::MaybeLocal<v8::Value> Events::InvokeHandler(v8::Isolate *isolate, v8::Local<v8::Context> context, CollectedHandler &handler, const std::vector<v8::Local<v8::Value>> &args, std::optional<std::vector<TransferredValue>> &copied, std::string_view eventName) {
        // Its isolate went since it was picked, with a resource an earlier handler stopped: there is nothing to call.
        if (handler.callback->IsEmpty()) {
            return {};
        }
        auto *monitor        = ResourceMonitor::Current();
        const double charged = monitor ? monitor->ChargedMs(handler.isolate, ResourceMonitor::Clock::now()) : 0.0;
        v8::MaybeLocal<v8::Value> result;
        if (handler.isolate == isolate) {
            ResourceMonitor::Scope scope(isolate);
            std::vector<v8::Local<v8::Value>> argv(args.begin(), args.end());
            result = handler.callback->Get(isolate)->Call(context, context->Global(), static_cast<int>(argv.size()), argv.empty() ? nullptr : argv.data());
        }
        else {
            // The handler's resource runs in a runtime of its own: call it there with the arguments copied across. What
            // it returns only matters as a veto or a promise to wait on, so its value is never copied back.
            FunctionReferences *references = _resourceManager != nullptr ? _resourceManager->GetFunctionReferences() : nullptr;
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
            result = references->Call(isolate, handler.isolate, *handler.callback, *copied, FunctionReferences::Returned::Outcome);
        }
        // Its own time only: copying the arguments was the emitter's, and resources it called into are theirs.
        if (monitor) {
            monitor->ReportOwnTime(handler.isolate, "handler for", eventName, charged, ResourceMonitor::Clock::now());
        }
        return result;
    }

    v8::Local<v8::Value> Events::InvokeCallable(v8::Isolate *isolate, v8::Local<v8::Context> context, CollectedHandler &handler, const std::vector<v8::Local<v8::Value>> &args, CallableArguments &copied, const std::string &eventName, std::string_view api) {
        if (!copied.Ensure(isolate, context, args, api, _resourceManager, handler.resourceName, eventName)) {
            v8::Local<v8::Promise::Resolver> errResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            errResolver->Reject(context, v8::Exception::Error(v8pp::to_v8(isolate, copied.error))).Check();
            return errResolver->GetPromise();
        }

        // The promise of this one handler, settled from wherever the handler ends: here when it finished at once,
        // from a later tick of its runtime when it waits. It is dropped with the isolate if that goes first.
        v8::Local<v8::Promise> promise;
        const std::shared_ptr<Bind::Detail::V8Deferred> settled = Bind::Detail::V8Defer(isolate, context, promise);
        if (settled == nullptr) {
            return v8::Undefined(isolate);
        }
        handler.callable->Invoke(copied.values, Bind::ReturnMode::Outcome, [settled, manager = static_cast<const void *>(_resourceManager), resourceName = handler.resourceName, eventName](Bind::CallOutcome &&outcome) {
            if (!outcome.threw) {
                settled->Resolve(std::move(outcome.value));
                return;
            }
            if (outcome.error != Bind::kRuntimeStopped) {
                LogHandlerError(manager, resourceName, eventName, outcome.error);
            }
            settled->Reject(FailureOf(outcome, resourceName));
        });
        settled->Returned(isolate, context);
        return promise;
    }

    v8::Local<v8::Array> Events::InvokeHandlersToPromiseArray(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<CollectedHandler> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName, std::string_view api) {
        v8::Local<v8::Array> promises = v8::Array::New(isolate, static_cast<int>(handlers.size()));
        std::optional<std::vector<TransferredValue>> copied;
        CallableArguments copiedForCallables;

        for (size_t i = 0; i < handlers.size(); ++i) {
            const std::string &logContext = handlers[i].resourceName;
            FW_PROFILE_SCOPE_N("Scripting::EventHandler");
            FW_PROFILE_TEXT(eventName.c_str(), eventName.size());
            FW_PROFILE_TEXT(logContext.c_str(), logContext.size());

            // A Lua handler: through its callable, unless its runtime went since it was picked.
            if (handlers[i].isolate == nullptr) {
                if (handlers[i].callable->IsAlive()) {
                    promises->Set(context, static_cast<uint32_t>(i), InvokeCallable(isolate, context, handlers[i], args, copiedForCallables, eventName, api)).Check();
                }
                else {
                    promises->Set(context, static_cast<uint32_t>(i), v8::Undefined(isolate)).Check();
                }
                continue;
            }

            v8::TryCatch tryCatch(isolate);
            v8::MaybeLocal<v8::Value> maybeResult = InvokeHandler(isolate, context, handlers[i], args, copied, eventName);

            if (tryCatch.HasCaught()) {
                // FormatV8Exception pulls the JS stack trace when available and
                // otherwise falls back to "<message>\n    at <file>:<line>:<col>",
                // so the script author can see where the throw actually happened
                // instead of just the bare error string.
                std::string errorStr = FormatV8Exception(isolate, tryCatch, "Unknown error in event handler");
                LogHandlerError(_resourceManager, logContext, eventName, errorStr);

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
                data->cancelled.store(true, std::memory_order_release);
                data->resolver.Reset();
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
        callbackData->isolate      = isolate;
        callbackData->errorMessage = aggregateErrorMessage;
        callbackData->owner        = this;

        {
            std::scoped_lock lock(_pendingCallbacksMutex);
            _pendingCallbacks.insert(callbackData);
        }

        // The handler holds a key to its own strong reference, so ~Events() clearing _pendingCallbacks can't free the
        // data out from under a still-pending microtask, and disposing the isolate can still free it.
        uint64_t key = 0;
        {
            std::scoped_lock lock(_waitingEmissionsMutex);
            key = _nextWaitingEmission++;
            _waitingEmissions.emplace(key, callbackData);
        }
        auto forget = [key]() {
            std::scoped_lock lock(_waitingEmissionsMutex);
            _waitingEmissions.erase(key);
        };
        v8::Local<v8::Value> resolverData = v8::BigInt::NewFromUnsigned(isolate, key);

        v8::MaybeLocal<v8::Function> maybeThenHandler = v8::Function::New(
            context,
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *iso           = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();

                // Take the data for this call (a then-handler runs once). Gone when the emitter's runtime was disposed.
                std::shared_ptr<AllSettledCallbackData> data;
                {
                    std::scoped_lock lock(_waitingEmissionsMutex);
                    const auto waiting = _waitingEmissions.find(info.Data().As<v8::BigInt>()->Uint64Value());
                    if (waiting == _waitingEmissions.end()) {
                        return;
                    }
                    data = std::move(waiting->second);
                    _waitingEmissions.erase(waiting);
                }

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
        // would abort. Fail soft: the handler won't run, so forget its data and untrack here.
        v8::Local<v8::Function> thenHandler;
        if (!maybeThenHandler.ToLocal(&thenHandler)) {
            forget();
            RemovePendingCallback(callbackData.get());
            return;
        }

        if (allPromise->Then(context, thenHandler).IsEmpty()) {
            forget();
            RemovePendingCallback(callbackData.get());
            return;
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

        LiveHandlers(it->second);

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

        TrackDispatch(handlersToCall);

        return handlersToCall;
    }

    std::vector<CollectedHandler> Events::CollectLocalHandlers(const std::string &resourceName, const std::string &eventName) {
        std::vector<CollectedHandler> handlersToCall;

        std::scoped_lock lock(_handlersMutex);
        auto resIt = _localHandlers.find(resourceName);
        if (resIt == _localHandlers.end()) {
            return handlersToCall;
        }
        auto evtIt = resIt->second.find(eventName);
        if (evtIt == resIt->second.end()) {
            return handlersToCall;
        }
        LiveHandlers(evtIt->second);
        for (const auto &handler : evtIt->second) {
            handlersToCall.emplace_back(handler);
        }
        TrackDispatch(handlersToCall);
        return handlersToCall;
    }

    void Events::TrackDispatch(const std::vector<CollectedHandler> &handlers) {
        // What the emissions that ended since held is let go of first, once for each isolate this one touches.
        std::vector<v8::Isolate *> pruned;
        for (const CollectedHandler &handler : handlers) {
            if (handler.isolate == nullptr) {
                continue;
            }
            auto &live = _dispatching[handler.isolate];
            if (std::find(pruned.begin(), pruned.end(), handler.isolate) == pruned.end()) {
                pruned.push_back(handler.isolate);
                std::erase_if(live, [](const std::weak_ptr<v8::Global<v8::Function>> &function) {
                    return function.expired();
                });
            }
            live.push_back(handler.callback);
        }
    }

    v8::Local<v8::Promise> Events::EmitToHandlers(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<CollectedHandler> &handlers, const std::vector<v8::Local<v8::Value>> &args, const std::string &eventName, const std::string &aggregateErrorMessage,
        std::string_view api) {
        v8::EscapableHandleScope handleScope(isolate);

        // Create Promise resolver
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise            = resolver->GetPromise();

        if (handlers.empty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return handleScope.Escape(promise);
        }

        // Invoke handlers and collect results as promises
        v8::Local<v8::Array> promises = InvokeHandlersToPromiseArray(isolate, context, handlers, args, eventName, api);

        // Aggregate results using Promise.allSettled
        AggregateWithAllSettled(isolate, context, promises, resolver, aggregateErrorMessage);

        return handleScope.Escape(promise);
    }

    v8::Local<v8::Promise> Events::EmitInternal(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &targetResource, HandlerScope scope, std::string_view api) {
        auto handlersToCall = CollectHandlers(eventName, targetResource, scope);
        return EmitToHandlers(isolate, context, handlersToCall, args, eventName, kHandlersFailed, api);
    }

    // What the handlers of one emission with copied arguments share: how many have not settled, and what those
    // that failed threw.
    struct Events::CopiedEmission final {
        size_t pending = 0;
        std::vector<std::optional<std::string>> failures; // by handler
        EmissionDone done;                                // empty once told, or once nobody waits any more

        void Settle(size_t index, std::optional<std::string> failure) {
            failures[index] = std::move(failure);
            if (--pending > 0) {
                return;
            }
            std::vector<std::string> errors;
            for (auto &entry : failures) {
                if (entry) {
                    errors.push_back(std::move(*entry));
                }
            }
            if (EmissionDone told = std::move(done)) {
                told(std::move(errors));
            }
        }
    };

    Events::EmissionToken Events::EmitCopied(const void *manager, std::vector<CollectedHandler> handlers, const std::vector<TransferredValue> &args, const std::string &eventName, EmissionDone done) {
        if (handlers.empty()) {
            if (done) {
                done({});
            }
            return nullptr;
        }

        const auto emission = std::make_shared<CopiedEmission>();
        emission->pending   = handlers.size();
        emission->failures.resize(handlers.size());
        emission->done = std::move(done);

        for (size_t i = 0; i < handlers.size(); ++i) {
            const CollectedHandler &handler = handlers[i];
            FW_PROFILE_SCOPE_N("Scripting::EventHandler");
            FW_PROFILE_TEXT(eventName.c_str(), eventName.size());
            FW_PROFILE_TEXT(handler.resourceName.c_str(), handler.resourceName.size());

            // An earlier handler may have stopped the runtime of this one.
            if (!handler.callable->IsAlive()) {
                emission->Settle(i, std::nullopt);
                continue;
            }

            // A JavaScript handler that is slow is reported under its own name, as when JavaScript emits.
            v8::Isolate *isolate = MonitoredIsolate(*handler.callable);
            auto *monitor        = isolate != nullptr ? ResourceMonitor::Current() : nullptr;
            const double charged = monitor ? monitor->ChargedMs(isolate, ResourceMonitor::Clock::now()) : 0.0;

            handler.callable->Invoke(args, Bind::ReturnMode::Outcome, [emission, i, manager, resourceName = handler.resourceName, eventName](Bind::CallOutcome &&outcome) {
                if (!outcome.threw) {
                    emission->Settle(i, std::nullopt);
                    return;
                }
                if (outcome.error != Bind::kRuntimeStopped) {
                    LogHandlerError(manager, resourceName, eventName, outcome.error);
                }
                emission->Settle(i, FailureOf(outcome, resourceName));
            });

            if (monitor) {
                monitor->ReportOwnTime(isolate, "handler for", eventName, charged, ResourceMonitor::Clock::now());
            }
        }
        return emission;
    }

    Events::EmissionToken Events::EmitReserved(const std::string &eventName, std::vector<TransferredValue> args, EmissionDone done) {
        return EmitCopied(_resourceManager, CollectHandlers(eventName, "", HandlerScope::Global), args, eventName, std::move(done));
    }

    void Events::CancelPendingEmission(const EmissionToken &emission) {
        if (emission != nullptr) {
            // Dropped, not told: the handlers that are still at it settle into nothing.
            emission->done = nullptr;
        }
    }

    v8::Local<v8::Promise> Events::EmitReserved(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args, "", HandlerScope::Global, "Events::EmitReserved");
    }

    bool Events::EmitReservedSync(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        v8::HandleScope handleScope(isolate);

        auto handlersToCall = CollectHandlers(eventName, "", HandlerScope::Global);
        bool proceed        = true;
        std::optional<std::vector<TransferredValue>> copied;
        CallableArguments copiedForCallables;

        for (auto &handler : handlersToCall) {
            const std::string &logContext = handler.resourceName;
            FW_PROFILE_SCOPE_N("Scripting::EventHandler");
            FW_PROFILE_TEXT(eventName.c_str(), eventName.size());
            FW_PROFILE_TEXT(logContext.c_str(), logContext.size());

            // A Lua handler: through its callable. Only what it returns before it first waits can veto, as a
            // JavaScript handler that returns a Promise cannot. What it fails with is logged whenever it ends.
            if (handler.isolate == nullptr) {
                if (!handler.callable->IsAlive()) {
                    continue;
                }
                if (!copiedForCallables.Ensure(isolate, context, args, "Events::EmitReservedSync", _resourceManager, logContext, eventName)) {
                    continue;
                }
                struct Verdict final {
                    bool listening = true;
                    bool veto      = false;
                };
                const auto verdict = std::make_shared<Verdict>();
                handler.callable->Invoke(copiedForCallables.values, Bind::ReturnMode::Outcome, [verdict, manager = static_cast<const void *>(_resourceManager), logContext, eventName](Bind::CallOutcome &&outcome) {
                    if (outcome.threw) {
                        if (outcome.error != Bind::kRuntimeStopped) {
                            LogHandlerError(manager, logContext, eventName, outcome.error);
                        }
                        return;
                    }
                    // Literal false only, as below.
                    if (verdict->listening && outcome.value.kind == TransferredValue::Kind::Boolean && !outcome.value.boolean) {
                        verdict->veto = true;
                    }
                });
                verdict->listening = false;
                if (verdict->veto) {
                    proceed = false;
                }
                continue;
            }

            v8::TryCatch tryCatch(isolate);

            v8::MaybeLocal<v8::Value> maybeResult = InvokeHandler(isolate, context, handler, args, copied, eventName);

            if (tryCatch.HasCaught()) {
                std::string errorStr = FormatV8Exception(isolate, tryCatch, "Unknown error in event handler");
                LogHandlerError(_resourceManager, logContext, eventName, errorStr);
                tryCatch.Reset();
                continue;
            }

            // Literal false only, so a handler that forgets to return can't silently veto.
            v8::Local<v8::Value> result;
            if (maybeResult.ToLocal(&result) && result->IsBoolean() && !result->IsTrue()) {
                proceed = false;
            }
        }

        return proceed;
    }

    v8::Local<v8::Promise> Events::EmitReservedTo(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args, const std::string &resourceName) {
        return EmitInternal(isolate, context, eventName, args, resourceName, HandlerScope::Global, "Events::EmitReservedTo");
    }

    v8::Local<v8::Promise> Events::EmitClient(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::string &eventName, const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args, "", HandlerScope::Client, "Events::EmitClient");
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

    void Events::UnregisterIsolate(v8::Isolate *isolate) {
        Events *events = nullptr;
        {
            std::scoped_lock lock(BindingsMutex());
            const auto bound = Bindings().find(isolate);
            if (bound != Bindings().end()) {
                events = bound->second.events;
            }
        }
        if (events != nullptr) {
            events->ForgetIsolate(isolate);
        }
    }

    void Events::ForgetIsolate(v8::Isolate *isolate) {
        {
            // The address may be the next isolate's.
            std::scoped_lock lock(BindingsMutex());
            const auto bound = Bindings().find(isolate);
            if (bound != Bindings().end() && bound->second.events == this) {
                Bindings().erase(bound);
            }
        }
        {
            std::scoped_lock lock(_handlersMutex);
            // Emptied as they go: an emission in progress may hold the function too.
            const auto ofIsolate = [isolate](const EventHandler &h) {
                if (h.isolate != isolate) {
                    return false;
                }
                h.callback->Reset();
                return true;
            };
            for (auto *table : {&_globalHandlers, &_clientHandlers}) {
                for (auto &[eventName, handlers] : *table) {
                    std::erase_if(handlers, ofIsolate);
                }
            }
            for (auto &[resourceName, eventMap] : _localHandlers) {
                for (auto &[eventName, handlers] : eventMap) {
                    std::erase_if(handlers, ofIsolate);
                }
            }

            // And those of handlers that left their table, by `off` or with their resource, while an emission
            // that picked them is still at it.
            if (const auto dispatching = _dispatching.find(isolate); dispatching != _dispatching.end()) {
                for (const auto &held : dispatching->second) {
                    if (const EventFunction function = held.lock()) {
                        function->Reset();
                    }
                }
                _dispatching.erase(dispatching);
            }
        }

        // An emission from this isolate still waiting on its handlers never settles now: its then-handler dies with
        // the isolate, so the data it would have taken is freed here.
        {
            std::scoped_lock lock(_pendingCallbacksMutex);
            for (auto it = _pendingCallbacks.begin(); it != _pendingCallbacks.end();) {
                if ((*it)->isolate == isolate) {
                    (*it)->cancelled.store(true, std::memory_order_release);
                    (*it)->resolver.Reset();
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
                    if (handler.callback) {
                        handler.callback->Reset();
                    }
                }
            }
            table->clear();
        }

        for (auto &[resourceName, eventMap] : _localHandlers) {
            for (auto &[eventName, handlers] : eventMap) {
                for (auto &handler : handlers) {
                    if (handler.callback) {
                        handler.callback->Reset();
                    }
                }
            }
        }
        _localHandlers.clear();
    }

    size_t Events::GetListenerCount(std::string_view eventName) {
        std::scoped_lock lock(_handlersMutex);
        auto it = _globalHandlers.find(eventName);
        if (it != _globalHandlers.end()) {
            return LiveHandlers(it->second);
        }
        return 0;
    }

    size_t Events::GetClientListenerCount(std::string_view eventName) {
        std::scoped_lock lock(_handlersMutex);
        auto it = _clientHandlers.find(eventName);
        if (it != _clientHandlers.end()) {
            return LiveHandlers(it->second);
        }
        return 0;
    }

} // namespace Framework::Scripting::Builtins
