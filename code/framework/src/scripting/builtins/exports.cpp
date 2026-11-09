/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "exports.h"
#include "../engine_helpers.h"
#include "../function_references.h"
#include "../resource/resource.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"

#include <logging/logger.h>

namespace Framework::Scripting::Builtins {

    void Exports::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager) {
        v8::Local<v8::Object> exportsObj = v8::Object::New(isolate);

        // Store resource manager as external data for callbacks
        v8::Local<v8::External> managerData = v8::External::New(isolate, resourceManager);

        // register(name, value) - Register an export from the current resource
        v8::Local<v8::FunctionTemplate> registerTmpl = v8::FunctionTemplate::New(isolate, RegisterCallback, managerData);
        exportsObj->Set(context, v8pp::to_v8(isolate, "register"), registerTmpl->GetFunction(context).ToLocalChecked()).Check();

        // get(resourceName, exportName) - Get an export from another resource
        v8::Local<v8::FunctionTemplate> getTmpl = v8::FunctionTemplate::New(isolate, GetCallback, managerData);
        exportsObj->Set(context, v8pp::to_v8(isolate, "get"), getTmpl->GetFunction(context).ToLocalChecked()).Check();

        target->Set(context, v8pp::to_v8(isolate, "Exports"), exportsObj).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("Exports", "Registration and lookup of cross-resource values through the global Exports object.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("register", v8pp::metadata::docs("boolean",
                                                                                          {
                                                                                              v8pp::metadata::param("name", "string", false, "Export name, preferably declared in the current resource manifest."),
                                                                                              v8pp::metadata::param("value", "unknown", false, "JavaScript value or function retained by the current resource."),
                                                                                          },
                                                                                          "Registers a value from the calling resource for use by dependent resources.", "True when the value was registered.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("get", v8pp::metadata::docs("unknown",
                                                                                     {
                                                                                         v8pp::metadata::param("resourceName", "string", false, "Name of the running resource that owns the export."),
                                                                                         v8pp::metadata::param("exportName", "string", false, "Registered export name."),
                                                                                     },
                                                                                     "Reads one registered export from another running resource; undeclared dependencies produce a warning.", "The exported value.")));
    }

    void Exports::RegisterCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            ThrowTypeError(isolate, "Exports.register requires 2 arguments: name, value");
            return;
        }

        if (!args[0]->IsString()) {
            ThrowTypeError(isolate, "Exports.register: name must be a string");
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register: resource manager not available")));
            return;
        }

        std::string exportName           = v8pp::from_v8<std::string>(isolate, args[0]);
        v8::Local<v8::Value> exportValue = args[1];

        // Get the current resource context (use stack fallback for async ES modules)
        Resource *currentResource = manager->GetCurrentResourceWithStackFallback(isolate);
        if (!currentResource) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register: no resource context")));
            return;
        }

        // Check if this export is declared in the manifest
        bool isInManifest = currentResource->HasExport(exportName);
        if (!isInManifest) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] Registering undeclared export '{}' - add to package.json exports array", currentResource->GetName(), exportName);
        }

        // Set the isolate for the resource (needed for storing exports)
        currentResource->SetIsolate(isolate);

        // A function registered directly is reported by this name when it has none of its own.
        if (exportValue->IsFunction()) {
            FunctionReferences::NameExport(isolate, isolate->GetCurrentContext(), exportValue.As<v8::Function>(), exportName);
        }

        // Register the export
        bool registered = currentResource->RegisterExport(exportName, exportValue);
        if (!registered) {
            if (!isInManifest) {
                // RegisterExport failed because it's not in the manifest - this is permissive
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Export '{}' registered (not in manifest)", currentResource->GetName(), exportName);
                args.GetReturnValue().Set(v8::True(isolate));
            }
            else {
                // Export is in manifest but RegisterExport still failed - real error (isolate issue)
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Failed to register export '{}' - isolate initialization issue", currentResource->GetName(), exportName);
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.register: failed to register export '" + exportName + "' - internal error")));
            }
        }
        else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Export '{}' registered", currentResource->GetName(), exportName);
            args.GetReturnValue().Set(v8::True(isolate));
        }
    }

    void Exports::GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 2) {
            ThrowTypeError(isolate, "Exports.get requires 2 arguments: resourceName, exportName");
            return;
        }

        if (!args[0]->IsString() || !args[1]->IsString()) {
            ThrowTypeError(isolate, "Exports.get: resourceName and exportName must be strings");
            return;
        }

        ResourceManager *manager = static_cast<ResourceManager *>(args.Data().As<v8::External>()->Value());
        if (!manager) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: resource manager not available")));
            return;
        }

        std::string resourceName = v8pp::from_v8<std::string>(isolate, args[0]);
        std::string exportName   = v8pp::from_v8<std::string>(isolate, args[1]);

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

        const std::string notFound = "Exports.get: export '" + exportName + "' not found in resource '" + resourceName + "'";

        // A resource in another language: its exports are functions, reached through a stand-in that copies the
        // arguments across and the result back.
        if (manager->GetRuntimeOf(resourceName) != nullptr) {
            if (!manager->GetRuntimeOf(resourceName)->HasExport(resourceName, exportName)) {
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, notFound)));
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> data      = v8::Array::New(isolate, 3);
            data->Set(context, 0, v8pp::to_v8(isolate, resourceName)).Check();
            data->Set(context, 1, v8pp::to_v8(isolate, exportName)).Check();
            data->Set(context, 2, v8::External::New(isolate, manager)).Check();
            v8::Local<v8::Function> standIn;
            if (v8::Function::New(context, ForeignExportCallback, data).ToLocal(&standIn)) {
                standIn->SetName(v8pp::to_v8(isolate, exportName));
                args.GetReturnValue().Set(standIn);
            }
            return;
        }

        // Export values are bound to their resource's isolate; nothing registered yet means none.
        v8::Isolate *resourceIsolate = resource->GetIsolate();
        if (resourceIsolate == nullptr) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, notFound)));
            return;
        }

        // Read inside whichever runtime holds the export.
        auto read = [&](v8::Isolate *owner) -> v8::MaybeLocal<v8::Value> {
            v8::Local<v8::Value> exportValue = resource->GetExportValue(exportName);
            if (exportValue.IsEmpty()) {
                owner->ThrowException(v8::Exception::Error(v8pp::to_v8(owner, notFound)));
                return {};
            }
            return exportValue;
        };

        if (resourceIsolate == isolate) {
            v8::Local<v8::Value> exportValue;
            if (read(isolate).ToLocal(&exportValue)) {
                args.GetReturnValue().Set(exportValue);
            }
            return;
        }

        // The resource runs in a runtime of its own: the export arrives as a copy, its functions as references.
        FunctionReferences *references = manager->GetFunctionReferences();
        if (references == nullptr) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Exports.get: resource '" + resourceName + "' is not reachable from here")));
            return;
        }
        v8::Local<v8::Value> copied;
        const bool fetched = references
                                 ->Fetch(isolate, resourceIsolate,
                                     [&](v8::Isolate *owner, v8::Local<v8::Context>) {
                                         return read(owner);
                                     })
                                 .ToLocal(&copied);
        if (fetched) {
            args.GetReturnValue().Set(copied);
        }
    }

    void Exports::ForeignExportCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate           = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::HandleScope handleScope(isolate);

        const auto data                = args.Data().As<v8::Array>();
        const std::string resourceName = v8pp::from_v8<std::string>(isolate, data->Get(context, 0).ToLocalChecked());
        const std::string exportName   = v8pp::from_v8<std::string>(isolate, data->Get(context, 1).ToLocalChecked());
        auto *manager                  = static_cast<ResourceManager *>(data->Get(context, 2).ToLocalChecked().As<v8::External>()->Value());
        const std::string label        = "Exports '" + resourceName + "." + exportName + "'";

        std::vector<TransferredValue> copied;
        copied.reserve(args.Length());
        for (int i = 0; i < args.Length(); ++i) {
            auto value = ValueTransfer::Copy(isolate, context, args[i]);
            if (!value) {
                ThrowTypeError(isolate, (label + ": argument " + std::to_string(i + 1) + " cannot cross into another language: " + value.GetError()).c_str());
                return;
            }
            copied.push_back(std::move(value).GetValue());
        }

        auto result = manager->CallExport(resourceName, exportName, copied);
        if (!result) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, label + ": " + result.GetError())));
            return;
        }
        v8::Local<v8::Value> rebuilt;
        if (ValueTransfer::Rebuild(isolate, context, result.GetValue()).ToLocal(&rebuilt)) {
            args.GetReturnValue().Set(rebuilt);
        }
    }

} // namespace Framework::Scripting::Builtins
