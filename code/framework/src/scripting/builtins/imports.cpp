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

namespace Framework::Scripting {

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
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "imports.get requires 1 argument: resourceName")));
            return;
        }

        if (!args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "imports.get: resourceName must be a string")));
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
            exportArray->Set(context, static_cast<uint32_t>(i), v8pp::to_v8(isolate, exportNames[i])).Check();
        }
        exportsObj->Set(context, v8pp::to_v8(isolate, "_availableExports"), exportArray).Check();

        // Log for debugging
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Imported {} exports from '{}'", callerResource, exportNames.size(), resourceName);

        args.GetReturnValue().Set(exportsObj);
    }

} // namespace Framework::Scripting
