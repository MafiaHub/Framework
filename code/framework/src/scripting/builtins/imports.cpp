/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "imports.h"
#include "exports.h"

#include "../engine_helpers.h"
#include "../function_references.h"
#include "../resource/resource.h"
#include "../resource/resource_manager.h"
#include "../script_log.h"
#include "../scripting_catalog.h"

#include <logging/logger.h>

namespace Framework::Scripting::Builtins {

    struct Imports::Script final {
        static void Get(Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowTypeError("Imports.get requires 1 argument: resourceName");
                return;
            }

            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Imports.get: resourceName must be a string");
                return;
            }

            ResourceManager *manager = call.GetResourceManager();
            if (!manager) {
                call.ThrowError("Imports.get: resource manager not available");
                return;
            }

            const bool javaScript            = call.GetLanguage() == Language::JavaScript;
            const std::string resourceName   = call.Arg(0).AsString();
            const std::string callerResource = javaScript ? manager->GetCurrentResourceContext() : call.ResourceName();

            // Check if target resource exists
            const Resource *resource = manager->GetResource(resourceName);
            if (!resource) {
                call.ThrowError("Imports.get: resource '" + resourceName + "' not found");
                return;
            }

            // Check if target resource is running
            if (!resource->IsRunning()) {
                call.ThrowError("Imports.get: resource '" + resourceName + "' is not running");
                return;
            }

            // Check if caller depends on target (optional warning)
            if (!callerResource.empty()) {
                const Resource *caller = manager->GetResource(callerResource);
                if (caller && !caller->DependsOn(resourceName)) {
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] Importing from '{}' without declaring dependency", callerResource, BlankedLine(resourceName));
                }
            }

            Bind::Value imports;
            if (javaScript) {
                v8::Isolate *isolate           = v8::Isolate::GetCurrent();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();

                // Export values are bound to their resource's isolate. In this one (or with none registered yet) the
                // object holds the real values; from a runtime of its own it arrives as a copy, its functions as
                // references.
                v8::Isolate *resourceIsolate = resource->GetIsolate();
                v8::Local<v8::Value> exportsObj;
                if (resourceIsolate == nullptr || resourceIsolate == isolate) {
                    exportsObj = BuildImportsObject(isolate, context, resource);
                }
                else {
                    FunctionReferences *references = manager->GetFunctionReferences();
                    if (references == nullptr) {
                        call.ThrowError("Imports.get: resource '" + resourceName + "' is not reachable from here");
                        return;
                    }
                    const bool fetched = references
                                             ->Fetch(isolate, resourceIsolate,
                                                 [&](v8::Isolate *owner, v8::Local<v8::Context> ownerContext) -> v8::MaybeLocal<v8::Value> {
                                                     return BuildImportsObject(owner, ownerContext, resource);
                                                 })
                                             .ToLocal(&exportsObj);
                    if (!fetched) {
                        return;
                    }
                }
                imports = Bind::V8ValueIn(call, exportsObj);
            }
            else {
                imports = call.NewObject();
            }

            // What JavaScript did not get above: the exports registered from Lua, and for a Lua importer those
            // registered from JavaScript too, copied in one visit to their isolate. Copies, their functions as
            // callables.
            const std::vector<std::string> exportNames = resource->GetRegisteredExportNames();
            const auto add                             = [&](const std::string &exportName, const TransferredValue &copy) {
                auto rebuilt = call.FromTransfer(copy);
                if (!rebuilt) {
                    call.ThrowError("Imports.get: export '" + exportName + "' of resource '" + resourceName + "': " + rebuilt.GetError());
                    return false;
                }
                imports.Set(exportName, rebuilt.GetValue());
                return true;
            };
            std::vector<std::string> fromJavaScript;
            for (const std::string &exportName : exportNames) {
                if (const std::shared_ptr<const TransferredValue> copy = resource->GetExportCopy(exportName)) {
                    if (!add(exportName, *copy)) {
                        return;
                    }
                }
                else if (!javaScript) {
                    fromJavaScript.push_back(exportName);
                }
            }
            const auto copies = Exports::CopyJavaScriptExports(*resource, fromJavaScript);
            if (!copies) {
                call.ThrowError("Imports.get: " + copies.GetError());
                return;
            }
            for (const auto &[exportName, copy] : copies.GetValue()) {
                if (!add(exportName, copy)) {
                    return;
                }
            }

            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Imported {} exports from '{}'", callerResource, exportNames.size(), BlankedLine(resourceName));

            call.Return(imports);
        }
    };

    const Bind::Namespace &Imports::Definition() {
        static const Bind::Namespace definition = Bind::Namespace("Imports", "Bulk access to values exported by another running resource through the global Imports object.")
                                                      .Function("get", &Script::Get,
                                                          Bind::Doc("Builds an object containing every currently registered export from another resource; undeclared dependencies produce a warning.")
                                                              .Param("resourceName", Bind::TypeOf<std::string>(), "Name of the running resource whose exports should be read.")
                                                              .ReturnType(Bind::Type::Custom("Record<string, unknown>", "table<string, any>"))
                                                              .Returns("Object keyed by export name, or an empty object when the resource has no exports."));
        return definition;
    }

    void Imports::Register(Bind::Registrar &registrar, Bind::Languages languages, ResourceManager *resourceManager) {
        // The functions carry no manager of their own: a call asks the one of its runtime.
        if (registrar.HasJavaScript(languages) && resourceManager != nullptr) {
            Bind::SetCallResourceManager(registrar.Isolate(), resourceManager);
        }
        registrar.Global(Definition(), languages);
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
