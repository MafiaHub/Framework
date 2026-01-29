#include "console.h"
#include "../resource/js_resource_manager.h"

#include <logging/logger.h>

#include <sstream>

namespace Framework::Scripting::JS {

    JSResourceManager *Console::_resourceManager = nullptr;

    void Console::Register(v8::Isolate *isolate,
                          v8::Local<v8::Context> context,
                          JSResourceManager *resourceManager) {
        _resourceManager = resourceManager;

        v8::Local<v8::Object> consoleObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // console.log
        v8::Local<v8::FunctionTemplate> logTmpl = v8::FunctionTemplate::New(isolate, LogCallback, managerData);
        consoleObj->Set(context, V8Helpers::ToV8String(isolate, "log"), logTmpl->GetFunction(context).ToLocalChecked()).Check();

        // console.info (alias for log)
        consoleObj->Set(context, V8Helpers::ToV8String(isolate, "info"), logTmpl->GetFunction(context).ToLocalChecked()).Check();

        // console.warn
        v8::Local<v8::FunctionTemplate> warnTmpl = v8::FunctionTemplate::New(isolate, WarnCallback, managerData);
        consoleObj->Set(context, V8Helpers::ToV8String(isolate, "warn"), warnTmpl->GetFunction(context).ToLocalChecked()).Check();

        // console.error
        v8::Local<v8::FunctionTemplate> errorTmpl = v8::FunctionTemplate::New(isolate, ErrorCallback, managerData);
        consoleObj->Set(context, V8Helpers::ToV8String(isolate, "error"), errorTmpl->GetFunction(context).ToLocalChecked()).Check();

        // console.debug
        v8::Local<v8::FunctionTemplate> debugTmpl = v8::FunctionTemplate::New(isolate, DebugCallback, managerData);
        consoleObj->Set(context, V8Helpers::ToV8String(isolate, "debug"), debugTmpl->GetFunction(context).ToLocalChecked()).Check();

        // Set as global console
        context->Global()->Set(context, V8Helpers::ToV8String(isolate, "console"), consoleObj).Check();
    }

    std::string Console::FormatArgs(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args) {
        std::ostringstream ss;

        for (int i = 0; i < args.Length(); ++i) {
            if (i > 0) {
                ss << " ";
            }

            v8::Local<v8::Value> value = args[i];

            if (value->IsString()) {
                ss << V8Helpers::GetString(isolate, value);
            } else if (value->IsNumber()) {
                double num = value->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0);
                if (num == static_cast<int64_t>(num)) {
                    ss << static_cast<int64_t>(num);
                } else {
                    ss << num;
                }
            } else if (value->IsBoolean()) {
                ss << (value->BooleanValue(isolate) ? "true" : "false");
            } else if (value->IsNull()) {
                ss << "null";
            } else if (value->IsUndefined()) {
                ss << "undefined";
            } else if (value->IsArray()) {
                v8::Local<v8::Array> arr = value.As<v8::Array>();
                ss << "[Array(" << arr->Length() << ")]";
            } else if (value->IsObject()) {
                v8::Local<v8::Object> obj = value.As<v8::Object>();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();

                // Try to get JSON representation
                v8::Local<v8::Object> json = context->Global()
                    ->Get(context, V8Helpers::ToV8String(isolate, "JSON")).ToLocalChecked().As<v8::Object>();
                v8::Local<v8::Function> stringify = json
                    ->Get(context, V8Helpers::ToV8String(isolate, "stringify")).ToLocalChecked().As<v8::Function>();

                v8::Local<v8::Value> jsonArgs[1] = {obj};
                v8::MaybeLocal<v8::Value> jsonResult = stringify->Call(context, json, 1, jsonArgs);

                if (!jsonResult.IsEmpty()) {
                    ss << V8Helpers::GetString(isolate, jsonResult.ToLocalChecked());
                } else {
                    ss << "[Object]";
                }
            } else {
                v8::String::Utf8Value str(isolate, value);
                ss << (*str ? *str : "<unknown>");
            }
        }

        return ss.str();
    }

    void Console::LogCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        std::string resourceName = manager ? manager->GetCurrentResourceContext() : "";
        std::string message = FormatArgs(isolate, args);

        if (!resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("[{}] {}", resourceName, message);
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("{}", message);
        }
    }

    void Console::WarnCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        std::string resourceName = manager ? manager->GetCurrentResourceContext() : "";
        std::string message = FormatArgs(isolate, args);

        if (!resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] {}", resourceName, message);
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("{}", message);
        }
    }

    void Console::ErrorCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        std::string resourceName = manager ? manager->GetCurrentResourceContext() : "";
        std::string message = FormatArgs(isolate, args);

        if (!resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] {}", resourceName, message);
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("{}", message);
        }
    }

    void Console::DebugCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        std::string resourceName = manager ? manager->GetCurrentResourceContext() : "";
        std::string message = FormatArgs(isolate, args);

        if (!resourceName.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] {}", resourceName, message);
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}", message);
        }
    }

} // namespace Framework::Scripting::JS
