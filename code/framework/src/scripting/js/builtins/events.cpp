#include "events.h"
#include "../resource/js_resource_manager.h"

#include <logging/logger.h>

namespace Framework::Scripting::JS {

    std::map<std::string, std::map<std::string, std::vector<v8::Global<v8::Function>>>> Events::_handlers;
    std::mutex Events::_handlersMutex;
    JSResourceManager *Events::_resourceManager = nullptr;

    void Events::Register(v8::Isolate *isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Object> frameworkObj,
                          JSResourceManager *resourceManager) {
        _resourceManager = resourceManager;

        v8::Local<v8::Object> eventsObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // on(eventName, handler)
        v8::Local<v8::FunctionTemplate> onTmpl = v8::FunctionTemplate::New(isolate, OnCallback, managerData);
        eventsObj->Set(context, V8Helpers::ToV8String(isolate, "on"), onTmpl->GetFunction(context).ToLocalChecked()).Check();

        // off(eventName, handler)
        v8::Local<v8::FunctionTemplate> offTmpl = v8::FunctionTemplate::New(isolate, OffCallback, managerData);
        eventsObj->Set(context, V8Helpers::ToV8String(isolate, "off"), offTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emit(eventName, ...args)
        v8::Local<v8::FunctionTemplate> emitTmpl = v8::FunctionTemplate::New(isolate, EmitCallback, managerData);
        eventsObj->Set(context, V8Helpers::ToV8String(isolate, "emit"), emitTmpl->GetFunction(context).ToLocalChecked()).Check();

        // emitTo(resourceName, eventName, ...args)
        v8::Local<v8::FunctionTemplate> emitToTmpl = v8::FunctionTemplate::New(isolate, EmitToCallback, managerData);
        eventsObj->Set(context, V8Helpers::ToV8String(isolate, "emitTo"), emitToTmpl->GetFunction(context).ToLocalChecked()).Check();

        frameworkObj->Set(context, V8Helpers::ToV8String(isolate, "events"), eventsObj).Check();
    }

    void Events::OnCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            V8Helpers::ThrowError(isolate, "events.on requires 2 arguments: eventName, handler");
            return;
        }

        if (!args[0]->IsString()) {
            V8Helpers::ThrowError(isolate, "events.on: eventName must be a string");
            return;
        }

        if (!args[1]->IsFunction()) {
            V8Helpers::ThrowError(isolate, "events.on: handler must be a function");
            return;
        }

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            V8Helpers::ThrowError(isolate, "events.on: resource manager not available");
            return;
        }

        std::string eventName = V8Helpers::GetString(isolate, args[0]);
        std::string resourceName = manager->GetCurrentResourceContext();

        if (resourceName.empty()) {
            V8Helpers::ThrowError(isolate, "events.on: must be called from within a resource");
            return;
        }

        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            _handlers[resourceName][eventName].emplace_back(isolate, handler);
        }
    }

    void Events::OffCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            V8Helpers::ThrowError(isolate, "events.off requires 2 arguments: eventName, handler");
            return;
        }

        if (!args[0]->IsString()) {
            V8Helpers::ThrowError(isolate, "events.off: eventName must be a string");
            return;
        }

        if (!args[1]->IsFunction()) {
            V8Helpers::ThrowError(isolate, "events.off: handler must be a function");
            return;
        }

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            return;
        }

        std::string eventName = V8Helpers::GetString(isolate, args[0]);
        std::string resourceName = manager->GetCurrentResourceContext();

        if (resourceName.empty()) {
            return;
        }

        v8::Local<v8::Function> handler = args[1].As<v8::Function>();

        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            auto resourceIt = _handlers.find(resourceName);
            if (resourceIt == _handlers.end()) {
                return;
            }

            auto eventIt = resourceIt->second.find(eventName);
            if (eventIt == resourceIt->second.end()) {
                return;
            }

            auto &handlers = eventIt->second;
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [&isolate, &handler](const v8::Global<v8::Function> &stored) {
                        return stored.Get(isolate)->StrictEquals(handler);
                    }),
                handlers.end());
        }
    }

    void Events::EmitCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1) {
            V8Helpers::ThrowError(isolate, "events.emit requires at least 1 argument: eventName");
            return;
        }

        if (!args[0]->IsString()) {
            V8Helpers::ThrowError(isolate, "events.emit: eventName must be a string");
            return;
        }

        std::string eventName = V8Helpers::GetString(isolate, args[0]);

        // Collect remaining arguments
        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 1; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        EmitGlobal(isolate, context, eventName, eventArgs);
    }

    void Events::EmitToCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            V8Helpers::ThrowError(isolate, "events.emitTo requires at least 2 arguments: resourceName, eventName");
            return;
        }

        if (!args[0]->IsString()) {
            V8Helpers::ThrowError(isolate, "events.emitTo: resourceName must be a string");
            return;
        }

        if (!args[1]->IsString()) {
            V8Helpers::ThrowError(isolate, "events.emitTo: eventName must be a string");
            return;
        }

        std::string resourceName = V8Helpers::GetString(isolate, args[0]);
        std::string eventName = V8Helpers::GetString(isolate, args[1]);

        // Collect remaining arguments
        std::vector<v8::Local<v8::Value>> eventArgs;
        for (int i = 2; i < args.Length(); ++i) {
            eventArgs.push_back(args[i]);
        }

        EmitToResource(isolate, context, resourceName, eventName, eventArgs);
    }

    void Events::EmitToResource(v8::Isolate *isolate,
                                 v8::Local<v8::Context> context,
                                 const std::string &resourceName,
                                 const std::string &eventName,
                                 const std::vector<v8::Local<v8::Value>> &args) {
        std::vector<v8::Global<v8::Function>> handlersToCall;

        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            auto resourceIt = _handlers.find(resourceName);
            if (resourceIt == _handlers.end()) {
                return;
            }

            auto eventIt = resourceIt->second.find(eventName);
            if (eventIt == resourceIt->second.end()) {
                return;
            }

            // Copy handlers to avoid holding lock during calls
            for (const auto &handler : eventIt->second) {
                handlersToCall.emplace_back(isolate, handler.Get(isolate));
            }
        }

        v8::TryCatch tryCatch(isolate);

        for (auto &handler : handlersToCall) {
            v8::Local<v8::Function> func = handler.Get(isolate);

            std::vector<v8::Local<v8::Value>> argv(args.begin(), args.end());

            v8::MaybeLocal<v8::Value> result = func->Call(context, context->Global(),
                                                          static_cast<int>(argv.size()),
                                                          argv.empty() ? nullptr : argv.data());

            if (tryCatch.HasCaught()) {
                v8::String::Utf8Value error(isolate, tryCatch.Exception());
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                    "[{}] Event '{}' handler error: {}",
                    resourceName, eventName, *error ? *error : "Unknown error");
                tryCatch.Reset();
            }
        }
    }

    void Events::EmitGlobal(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            const std::string &eventName,
                            const std::vector<v8::Local<v8::Value>> &args) {
        std::vector<std::string> resourceNames;

        {
            std::lock_guard<std::mutex> lock(_handlersMutex);
            for (const auto &[resourceName, events] : _handlers) {
                if (events.find(eventName) != events.end()) {
                    resourceNames.push_back(resourceName);
                }
            }
        }

        for (const auto &resourceName : resourceNames) {
            if (_resourceManager && _resourceManager->IsResourceRunning(resourceName)) {
                EmitToResource(isolate, context, resourceName, eventName, args);
            }
        }
    }

} // namespace Framework::Scripting::JS
