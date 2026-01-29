#include "imports.h"
#include "../resource/js_resource_manager.h"
#include "../resource/js_resource.h"

#include <logging/logger.h>

namespace Framework::Scripting::JS {

    JSResourceManager *Imports::_resourceManager = nullptr;

    void Imports::Register(v8::Isolate *isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Object> frameworkObj,
                          JSResourceManager *resourceManager) {
        _resourceManager = resourceManager;

        v8::Local<v8::Object> importsObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // get(resourceName)
        v8::Local<v8::FunctionTemplate> getTmpl = v8::FunctionTemplate::New(isolate, GetCallback, managerData);
        importsObj->Set(context, V8Helpers::ToV8String(isolate, "get"), getTmpl->GetFunction(context).ToLocalChecked()).Check();

        frameworkObj->Set(context, V8Helpers::ToV8String(isolate, "imports"), importsObj).Check();
    }

    void Imports::GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1) {
            V8Helpers::ThrowError(isolate, "imports.get requires 1 argument: resourceName");
            return;
        }

        if (!args[0]->IsString()) {
            V8Helpers::ThrowError(isolate, "imports.get: resourceName must be a string");
            return;
        }

        JSResourceManager *manager = static_cast<JSResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            V8Helpers::ThrowError(isolate, "imports.get: resource manager not available");
            return;
        }

        std::string resourceName = V8Helpers::GetString(isolate, args[0]);
        std::string callerResource = manager->GetCurrentResourceContext();

        // Check if target resource exists
        const JSResource *resource = manager->GetResource(resourceName);
        if (!resource) {
            V8Helpers::ThrowError(isolate, ("imports.get: resource '" + resourceName + "' not found").c_str());
            return;
        }

        // Check if target resource is running
        if (!resource->IsRunning()) {
            V8Helpers::ThrowError(isolate, ("imports.get: resource '" + resourceName + "' is not running").c_str());
            return;
        }

        // Check if caller depends on target (optional warning)
        if (!callerResource.empty()) {
            const JSResource *caller = manager->GetResource(callerResource);
            if (caller && !caller->DependsOn(resourceName)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                    "[{}] Importing from '{}' without declaring dependency",
                    callerResource, resourceName);
            }
        }

        // Get registered export names and build an object
        std::vector<std::string> exportNames = resource->GetRegisteredExportNames();

        if (exportNames.empty()) {
            // Return empty object if no exports
            args.GetReturnValue().Set(v8::Object::New(isolate));
            return;
        }

        // Note: The current implementation stores exports as v8::Global<v8::Value>
        // We need to access them from the resource. For now, return an object
        // that indicates the exports are available but we need the isolate
        // from the target resource to actually retrieve them.

        // For cross-resource exports to work properly, we need a shared context
        // or value serialization. For now, return the list of available exports.

        v8::Local<v8::Object> exportsObj = v8::Object::New(isolate);

        // Add a special property listing available exports
        v8::Local<v8::Array> exportArray = v8::Array::New(isolate, static_cast<int>(exportNames.size()));
        for (size_t i = 0; i < exportNames.size(); ++i) {
            exportArray->Set(context, static_cast<uint32_t>(i), V8Helpers::ToV8String(isolate, exportNames[i])).Check();
        }
        exportsObj->Set(context, V8Helpers::ToV8String(isolate, "_availableExports"), exportArray).Check();

        // Log for debugging
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug(
            "[{}] Imported {} exports from '{}'",
            callerResource, exportNames.size(), resourceName);

        args.GetReturnValue().Set(exportsObj);
    }

} // namespace Framework::Scripting::JS
