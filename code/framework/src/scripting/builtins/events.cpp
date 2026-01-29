#include "events.h"
#include "../resource/resource_manager.h"

#include <logging/logger.h>

namespace Framework::Scripting {

    std::map<std::string, std::vector<EventHandler>> Events::_globalHandlers;
    std::map<std::string, std::map<std::string, std::vector<EventHandler>>> Events::_localHandlers;
    std::mutex Events::_handlersMutex;
    ResourceManager *Events::_resourceManager = nullptr;

    void Events::Register(v8::Isolate *isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Object> global,
                          ResourceManager *resourceManager) {
        _resourceManager = resourceManager;

        v8::Local<v8::Object> eventsObj = v8::Object::New(isolate);
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // on(eventName, handler) -> unsubscribe function
        v8::Local<v8::FunctionTemplate> onTmpl = v8::FunctionTemplate::New(isolate, OnCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "on"), onTmpl->GetFunction(context).ToLocalChecked()).Check();

        // once(eventName, handler)
        v8::Local<v8::FunctionTemplate> onceTmpl = v8::FunctionTemplate::New(isolate, OnceCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "once"), onceTmpl->GetFunction(context).ToLocalChecked()).Check();

        // off(eventName, handler)
        v8::Local<v8::FunctionTemplate> offTmpl = v8::FunctionTemplate::New(isolate, OffCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "off"), offTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emit(eventName, ...args) -> Promise
        v8::Local<v8::FunctionTemplate> emitTmpl = v8::FunctionTemplate::New(isolate, EmitCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "emit"), emitTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emitTo(resourceName, eventName, ...args) -> Promise
        v8::Local<v8::FunctionTemplate> emitToTmpl = v8::FunctionTemplate::New(isolate, EmitToCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "emitTo"), emitToTmpl->GetFunction(context).ToLocalChecked()).Check();

        // onLocal(eventName, handler)
        v8::Local<v8::FunctionTemplate> onLocalTmpl = v8::FunctionTemplate::New(isolate, OnLocalCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "onLocal"), onLocalTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emitLocal(eventName, ...args) -> Promise
        v8::Local<v8::FunctionTemplate> emitLocalTmpl = v8::FunctionTemplate::New(isolate, EmitLocalCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "emitLocal"), emitLocalTmpl->GetFunction(context).ToLocalChecked()).Check();

        // listenerCount(eventName) -> number
        v8::Local<v8::FunctionTemplate> countTmpl = v8::FunctionTemplate::New(isolate, ListenerCountCallback, managerData);
        eventsObj->Set(context, v8pp::to_v8(isolate, "listenerCount"), countTmpl->GetFunction(context).ToLocalChecked()).Check();

        // Register as global "Events"
        global->Set(context, v8pp::to_v8(isolate, "Events"), eventsObj).Check();
    }

    // Helper to get resource context - uses V8 stack trace as fallback for async ES modules
    std::string GetResourceContextWithFallback(v8::Isolate *isolate, ResourceManager *manager) {
        std::string resourceName = manager->GetCurrentResourceContext();
        if (!resourceName.empty()) {
            return resourceName;
        }

        // Fallback: extract resource name from V8 call stack file paths
        return manager->GetResourceContextFromStack(isolate);
    }

    void Events::RegisterHandler(v8::Isolate *isolate,
                                  ResourceManager *manager,
                                  const std::string &eventName,
                                  v8::Local<v8::Function> handler,
                                  bool once) {
        std::string resourceName = GetResourceContextWithFallback(isolate, manager);
        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.on: must be called from within a resource")));
            return;
        }

        EventHandler entry;
        entry.callback.Reset(isolate, handler);
        entry.resourceName = resourceName;
        entry.once = once;

        std::lock_guard<std::mutex> lock(_handlersMutex);
        _globalHandlers[eventName].push_back(std::move(entry));
    }

    void Events::OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.on requires 2 arguments: eventName, handler")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.on: eventName must be a string")));
            return;
        }

        if (!args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.on: handler must be a function")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.on: resource manager not available")));
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();
        std::string resourceName = GetResourceContextWithFallback(isolate, manager);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.on: must be called from within a resource")));
            return;
        }

        RegisterHandler(isolate, manager, eventName, handler, false);

        // Return unsubscribe function
        v8::Local<v8::Object> data = v8::Object::New(isolate);
        data->Set(context, v8pp::to_v8(isolate, "eventName"), args[0]).Check();
        data->Set(context, v8pp::to_v8(isolate, "handler"), args[1]).Check();
        data->Set(context, v8pp::to_v8(isolate, "resourceName"), v8pp::to_v8(isolate, resourceName)).Check();

        v8::Local<v8::Function> unsubscribe = v8::Function::New(context,
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Object> d = info.Data().As<v8::Object>();

                v8::Local<v8::Value> evtVal = d->Get(ctx, v8pp::to_v8(iso, "eventName")).ToLocalChecked();
                v8::Local<v8::Value> hndVal = d->Get(ctx, v8pp::to_v8(iso, "handler")).ToLocalChecked();
                v8::Local<v8::Value> resVal = d->Get(ctx, v8pp::to_v8(iso, "resourceName")).ToLocalChecked();

                std::string evt = v8pp::from_v8<std::string>(iso, evtVal);
                v8::Local<v8::Function> hnd = hndVal.As<v8::Function>();
                std::string resName = v8pp::from_v8<std::string>(iso, resVal);

                std::lock_guard<std::mutex> lock(_handlersMutex);
                auto it = _globalHandlers.find(evt);
                if (it != _globalHandlers.end()) {
                    auto &handlers = it->second;
                    handlers.erase(
                        std::remove_if(handlers.begin(), handlers.end(),
                            [&](const EventHandler &h) {
                                return h.resourceName == resName &&
                                       h.callback.Get(iso)->StrictEquals(hnd);
                            }),
                        handlers.end());
                }
            }, data).ToLocalChecked();

        args.GetReturnValue().Set(unsubscribe);
    }

    void Events::OnceCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.once requires 2 arguments: eventName, handler")));
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.once: invalid arguments")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        RegisterHandler(isolate, manager, eventName, handler, true);
    }

    void Events::OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.off requires 2 arguments: eventName, handler")));
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsFunction()) {
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string resourceName = GetResourceContextWithFallback(isolate, manager);
        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        // If no resource context, we can't determine which resource to remove handlers for
        if (resourceName.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(_handlersMutex);
        auto it = _globalHandlers.find(eventName);
        if (it != _globalHandlers.end()) {
            auto &handlers = it->second;
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [&](const EventHandler &h) {
                        return h.resourceName == resourceName &&
                               h.callback.Get(isolate)->StrictEquals(handler);
                    }),
                handlers.end());
        }
    }

    v8::Local<v8::Promise> Events::EmitInternal(v8::Isolate *isolate,
                                                 v8::Local<v8::Context> context,
                                                 const std::string &eventName,
                                                 const std::vector<v8::Local<v8::Value>> &args,
                                                 const std::string &targetResource) {
        v8::EscapableHandleScope handleScope(isolate);

        // Create Promise resolver
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::Local<v8::Promise> promise = resolver->GetPromise();

        // Collect handlers to call and track which ones to remove (once handlers)
        std::vector<std::pair<v8::Global<v8::Function>, std::string>> handlersToCall;
        std::vector<size_t> indicesToRemove;

        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            auto it = _globalHandlers.find(eventName);
            if (it != _globalHandlers.end()) {
                for (size_t idx = 0; idx < it->second.size(); ++idx) {
                    const auto &handler = it->second[idx];

                    // Filter by target resource if specified
                    if (!targetResource.empty() && handler.resourceName != targetResource) {
                        continue;
                    }

                    // Check if resource is still running
                    if (_resourceManager && !_resourceManager->IsResourceRunning(handler.resourceName)) {
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

        // Collect all handler results/promises
        v8::Local<v8::Array> promises = v8::Array::New(isolate, static_cast<int>(handlersToCall.size()));
        bool hadErrors = false;

        for (size_t i = 0; i < handlersToCall.size(); ++i) {
            auto &[callback, resName] = handlersToCall[i];
            v8::Local<v8::Function> func = callback.Get(isolate);

            v8::TryCatch tryCatch(isolate);
            std::vector<v8::Local<v8::Value>> argv(args.begin(), args.end());

            v8::MaybeLocal<v8::Value> maybeResult = func->Call(context, context->Global(),
                                                                static_cast<int>(argv.size()),
                                                                argv.empty() ? nullptr : argv.data());

            if (tryCatch.HasCaught()) {
                hadErrors = true;
                v8::String::Utf8Value error(isolate, tryCatch.Exception());
                std::string errorStr = *error ? *error : "Unknown error";
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                    "[{}] Event '{}' handler error: {}", resName, eventName, errorStr);

                // Create rejected promise for this handler
                v8::Local<v8::Promise::Resolver> errResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                errResolver->Reject(context, tryCatch.Exception()).Check();
                promises->Set(context, static_cast<uint32_t>(i), errResolver->GetPromise()).Check();
                tryCatch.Reset();
            } else if (!maybeResult.IsEmpty()) {
                v8::Local<v8::Value> result = maybeResult.ToLocalChecked();

                // Wrap non-promise values in resolved promise
                if (result->IsPromise()) {
                    promises->Set(context, static_cast<uint32_t>(i), result).Check();
                } else {
                    v8::Local<v8::Promise::Resolver> valResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                    valResolver->Resolve(context, result).Check();
                    promises->Set(context, static_cast<uint32_t>(i), valResolver->GetPromise()).Check();
                }
            } else {
                // Empty result - create resolved promise with undefined
                v8::Local<v8::Promise::Resolver> valResolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                valResolver->Resolve(context, v8::Undefined(isolate)).Check();
                promises->Set(context, static_cast<uint32_t>(i), valResolver->GetPromise()).Check();
            }
        }

        // Use Promise.allSettled to wait for all handlers
        v8::Local<v8::Object> promiseConstructor = context->Global()
            ->Get(context, v8pp::to_v8(isolate, "Promise")).ToLocalChecked().As<v8::Object>();
        v8::Local<v8::Function> allSettled = promiseConstructor
            ->Get(context, v8pp::to_v8(isolate, "allSettled")).ToLocalChecked().As<v8::Function>();

        v8::Local<v8::Value> allSettledArgs[] = { promises };
        v8::MaybeLocal<v8::Value> allSettledResult = allSettled->Call(context, promiseConstructor, 1, allSettledArgs);

        if (allSettledResult.IsEmpty()) {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
            return handleScope.Escape(promise);
        }

        v8::Local<v8::Promise> allPromise = allSettledResult.ToLocalChecked().As<v8::Promise>();

        // Store resolver in persistent handle for the callback
        v8::Global<v8::Promise::Resolver> *persistentResolver = new v8::Global<v8::Promise::Resolver>(isolate, resolver);

        v8::Local<v8::External> resolverData = v8::External::New(isolate, persistentResolver);

        v8::Local<v8::Function> thenHandler = v8::Function::New(context,
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();

                v8::Global<v8::Promise::Resolver> *presolver =
                    static_cast<v8::Global<v8::Promise::Resolver> *>(info.Data().As<v8::External>()->Value());
                v8::Local<v8::Promise::Resolver> res = presolver->Get(iso);

                v8::Local<v8::Array> results = info[0].As<v8::Array>();
                std::vector<v8::Local<v8::Value>> rejections;

                for (uint32_t i = 0; i < results->Length(); ++i) {
                    v8::Local<v8::Object> item = results->Get(ctx, i).ToLocalChecked().As<v8::Object>();
                    v8::Local<v8::Value> status = item->Get(ctx, v8pp::to_v8(iso, "status")).ToLocalChecked();
                    v8::String::Utf8Value statusStr(iso, status);

                    if (std::string(*statusStr) == "rejected") {
                        v8::Local<v8::Value> reason = item->Get(ctx, v8pp::to_v8(iso, "reason")).ToLocalChecked();
                        rejections.push_back(reason);
                    }
                }

                if (!rejections.empty()) {
                    // Create AggregateError
                    v8::Local<v8::Array> errArray = v8::Array::New(iso, static_cast<int>(rejections.size()));
                    for (size_t i = 0; i < rejections.size(); ++i) {
                        errArray->Set(ctx, static_cast<uint32_t>(i), rejections[i]).Check();
                    }

                    v8::Local<v8::String> msg = v8pp::to_v8(iso, "One or more event handlers failed");
                    v8::Local<v8::Value> aggArgs[] = { errArray, msg };
                    v8::MaybeLocal<v8::Value> aggErrorCtorVal = ctx->Global()->Get(ctx, v8pp::to_v8(iso, "AggregateError"));

                    if (!aggErrorCtorVal.IsEmpty() && aggErrorCtorVal.ToLocalChecked()->IsFunction()) {
                        v8::Local<v8::Function> aggErrorCtor = aggErrorCtorVal.ToLocalChecked().As<v8::Function>();
                        v8::MaybeLocal<v8::Object> aggErrorObj = aggErrorCtor->NewInstance(ctx, 2, aggArgs);
                        if (!aggErrorObj.IsEmpty()) {
                            res->Reject(ctx, aggErrorObj.ToLocalChecked()).Check();
                        } else {
                            res->Reject(ctx, errArray).Check();
                        }
                    } else {
                        // Fallback if AggregateError not available
                        res->Reject(ctx, errArray).Check();
                    }
                } else {
                    res->Resolve(ctx, v8::Undefined(iso)).Check();
                }

                delete presolver;
            }, resolverData).ToLocalChecked();

        allPromise->Then(context, thenHandler).ToLocalChecked();

        return handleScope.Escape(promise);
    }

    v8::Local<v8::Promise> Events::EmitReserved(v8::Isolate *isolate,
                                                 v8::Local<v8::Context> context,
                                                 const std::string &eventName,
                                                 const std::vector<v8::Local<v8::Value>> &args) {
        return EmitInternal(isolate, context, eventName, args);
    }

    void Events::EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emit requires at least 1 argument: eventName")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emit: eventName must be a string")));
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);

        // Check reserved events
        if (IsReservedEvent(eventName)) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emit: cannot emit reserved event '" + eventName + "'")));
            return;
        }

        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 1; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        v8::Local<v8::Promise> promise = EmitInternal(isolate, context, eventName, eventArgs);
        args.GetReturnValue().Set(promise);
    }

    void Events::EmitToCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emitTo requires at least 2 arguments: resourceName, eventName")));
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emitTo: resourceName and eventName must be strings")));
            return;
        }

        std::string resourceName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string eventName = v8pp::from_v8<std::string>(isolate, args[1]);

        // Check reserved events
        if (IsReservedEvent(eventName)) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emitTo: cannot emit reserved event '" + eventName + "'")));
            return;
        }

        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 2; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        v8::Local<v8::Promise> promise = EmitInternal(isolate, context, eventName, eventArgs, resourceName);
        args.GetReturnValue().Set(promise);
    }

    void Events::OnLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.onLocal requires 2 arguments: eventName, handler")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string resourceName = GetResourceContextWithFallback(isolate, manager);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.onLocal: must be called from within a resource")));
            return;
        }

        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        EventHandler entry;
        entry.callback.Reset(isolate, handler);
        entry.resourceName = resourceName;
        entry.once = false;

        std::lock_guard<std::mutex> lock(_handlersMutex);
        _localHandlers[resourceName][eventName].push_back(std::move(entry));
    }

    void Events::EmitLocalCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1 || !args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emitLocal requires at least 1 argument: eventName")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            return;
        }

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string resourceName = GetResourceContextWithFallback(isolate, manager);

        if (resourceName.empty()) {
            isolate->ThrowException(v8::Exception::Error(
                v8pp::to_v8(isolate, "Events.emitLocal: must be called from within a resource")));
            return;
        }

        // Collect local handlers
        std::vector<v8::Global<v8::Function>> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            auto resIt = _localHandlers.find(resourceName);
            if (resIt != _localHandlers.end()) {
                auto evtIt = resIt->second.find(eventName);
                if (evtIt != resIt->second.end()) {
                    for (const auto &handler : evtIt->second) {
                        v8::Global<v8::Function> copy;
                        copy.Reset(isolate, handler.callback.Get(isolate));
                        handlersToCall.push_back(std::move(copy));
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

        // Call handlers
        std::vector<std::string> errors;
        for (auto &callback : handlersToCall) {
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Function> func = callback.Get(isolate);

            std::vector<v8::Local<v8::Value>> argv(eventArgs.begin(), eventArgs.end());
            func->Call(context, context->Global(), static_cast<int>(argv.size()),
                       argv.empty() ? nullptr : argv.data());

            if (tryCatch.HasCaught()) {
                v8::String::Utf8Value error(isolate, tryCatch.Exception());
                errors.push_back(*error ? *error : "Unknown error");
                tryCatch.Reset();
            }
        }

        if (!errors.empty()) {
            resolver->Reject(context, v8::Exception::Error(
                v8pp::to_v8(isolate, "Local event handler(s) failed"))).Check();
        } else {
            resolver->Resolve(context, v8::Undefined(isolate)).Check();
        }

        args.GetReturnValue().Set(resolver->GetPromise());
    }

    void Events::CleanupResource(const std::string &resourceName) {
        std::lock_guard<std::mutex> lock(_handlersMutex);

        // Remove from global handlers
        for (auto &[eventName, handlers] : _globalHandlers) {
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [&](const EventHandler &h) { return h.resourceName == resourceName; }),
                handlers.end());
        }

        // Remove local handlers entirely
        _localHandlers.erase(resourceName);
    }

    size_t Events::GetListenerCount(const std::string &eventName) {
        std::lock_guard<std::mutex> lock(_handlersMutex);
        auto it = _globalHandlers.find(eventName);
        if (it != _globalHandlers.end()) {
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

        std::string eventName = v8pp::from_v8<std::string>(isolate, args[0]);
        size_t count = GetListenerCount(eventName);
        args.GetReturnValue().Set(static_cast<uint32_t>(count));
    }

} // namespace Framework::Scripting
