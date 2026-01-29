#include "exports.h"
#include "../resource/resource_manager.h"
#include "../resource/resource.h"

#include <logging/logger.h>

namespace Framework::Scripting {

    ResourceManager *Exports::_resourceManager = nullptr;

    void Exports::Register(v8::Isolate *isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Object> frameworkObj,
                          ResourceManager *resourceManager) {
        _resourceManager = resourceManager;

        v8::Local<v8::Object> exportsObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // register(name, value) - Register an export from the current resource
        v8::Local<v8::FunctionTemplate> registerTmpl = v8::FunctionTemplate::New(isolate, RegisterCallback, managerData);
        exportsObj->Set(context, v8pp::to_v8(isolate, "register"), registerTmpl->GetFunction(context).ToLocalChecked()).Check();

        // get(resourceName, exportName) - Get an export from another resource
        v8::Local<v8::FunctionTemplate> getTmpl = v8::FunctionTemplate::New(isolate, GetCallback, managerData);
        exportsObj->Set(context, v8pp::to_v8(isolate, "get"), getTmpl->GetFunction(context).ToLocalChecked()).Check();

        frameworkObj->Set(context, v8pp::to_v8(isolate, "Exports"), exportsObj).Check();
    }

    void Exports::RegisterCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register requires 2 arguments: name, value")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register: name must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register: resource manager not available")));
            return;
        }

        std::string exportName = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Value> exportValue = args[1];

        // Get the current resource context (use stack fallback for async ES modules)
        Resource *currentResource = manager->GetCurrentResourceWithStackFallback(isolate);
        if (!currentResource) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register: no resource context")));
            return;
        }

        // Check if this export is declared in the manifest
        if (!currentResource->HasExport(exportName)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "[{}] Registering undeclared export '{}' - add to package.json exports array",
                currentResource->GetName(), exportName);
        }

        // Set the isolate for the resource (needed for storing exports)
        currentResource->SetIsolate(isolate);

        // Register the export
        if (!currentResource->RegisterExport(exportName, exportValue)) {
            // If RegisterExport failed because it's not in the manifest, still allow it but warn
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug(
                "[{}] Export '{}' registered (not in manifest)",
                currentResource->GetName(), exportName);
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug(
                "[{}] Export '{}' registered",
                currentResource->GetName(), exportName);
        }

        args.GetReturnValue().Set(v8::True(isolate));
    }

    void Exports::GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get requires 2 arguments: resourceName, exportName")));
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: resourceName and exportName must be strings")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: resource manager not available")));
            return;
        }

        std::string resourceName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string exportName = v8pp::from_v8<std::string>(isolate, args[1]);

        // Get the target resource
        const Resource *resource = manager->GetResource(resourceName);
        if (!resource) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: resource '" + resourceName + "' not found")));
            return;
        }

        // Check if the resource is running
        if (!resource->IsRunning()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: resource '" + resourceName + "' is not running")));
            return;
        }

        // Get the export value
        v8::Local<v8::Value> exportValue = resource->GetExportValue(exportName);
        if (exportValue.IsEmpty()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: export '" + exportName + "' not found in resource '" + resourceName + "'")));
            return;
        }

        args.GetReturnValue().Set(exportValue);
    }

} // namespace Framework::Scripting
