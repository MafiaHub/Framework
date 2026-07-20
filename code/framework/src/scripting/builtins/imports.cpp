/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "imports.h"
#include "../resource/resource.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"

#include <logging/logger.h>

namespace Framework::Scripting::Builtins {

    ResourceManager *Imports::_resourceManager = nullptr;

    void Imports::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> frameworkObj, ResourceManager *resourceManager) {
        _resourceManager = resourceManager;

        v8::Local<v8::Object> importsObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // get(resourceName)
        v8::Local<v8::FunctionTemplate> getTmpl = v8::FunctionTemplate::New(isolate, GetCallback, managerData);
        importsObj->Set(context, v8pp::to_v8(isolate, "get"), getTmpl->GetFunction(context).ToLocalChecked()).Check();

        frameworkObj->Set(context, v8pp::to_v8(isolate, "imports"), importsObj).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("imports", "Bulk access to values exported by another running resource through Framework.imports.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("get",
            v8pp::metadata::docs("Record<string, unknown>",
                {
                    v8pp::metadata::param("resourceName", "string", false, "Name of the running resource whose exports should be read."),
                },
                "Builds an object containing every currently registered export from another resource; undeclared dependencies produce a warning.", "Object keyed by export name, or an empty object when the resource has no exports.")));
    }

    void Imports::GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "imports.get requires 1 argument: resourceName")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "imports.get: resourceName must be a string")));
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "imports.get: resource manager not available")));
            return;
        }

        std::string resourceName   = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string callerResource = manager->GetCurrentResourceContext();

        // Check if target resource exists
        const Resource *resource = manager->GetResource(resourceName);
        if (!resource) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "imports.get: resource '" + resourceName + "' not found")));
            return;
        }

        // Check if target resource is running
        if (!resource->IsRunning()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "imports.get: resource '" + resourceName + "' is not running")));
            return;
        }

        // Check if caller depends on target (optional warning)
        if (!callerResource.empty()) {
            const Resource *caller = manager->GetResource(callerResource);
            if (caller && !caller->DependsOn(resourceName)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] Importing from '{}' without declaring dependency", callerResource, resourceName);
            }
        }

        // Validate isolate ownership - export values are bound to their resource's isolate.
        // Mirrors Exports::GetCallback: V8 values cannot be shared across isolates.
        v8::Isolate *resourceIsolate = resource->GetIsolate();
        if (resourceIsolate != isolate) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "imports.get: cannot access exports from resource '" + resourceName + "' - cross-isolate access is not supported. Both resources must share the same isolate.")));
            return;
        }

        // Build an object mapping every registered export name to its real value.
        v8::Local<v8::Object> exportsObj = BuildImportsObject(isolate, context, resource);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Imported {} exports from '{}'", callerResource, resource->GetRegisteredExportNames().size(), resourceName);

        args.GetReturnValue().Set(exportsObj);
    }

    v8::Local<v8::Object> Imports::BuildImportsObject(v8::Isolate *isolate, v8::Local<v8::Context> context, const Resource *resource) {
        v8::Local<v8::Object> exportsObj = v8::Object::New(isolate);
        if (!resource) {
            return exportsObj;
        }

        for (const std::string &exportName : resource->GetRegisteredExportNames()) {
            v8::Local<v8::Value> exportValue = resource->GetExportValue(exportName);
            if (exportValue.IsEmpty()) {
                continue;
            }
            exportsObj->Set(context, v8pp::to_v8(isolate, exportName), exportValue).Check();
        }

        return exportsObj;
    }

} // namespace Framework::Scripting::Builtins
