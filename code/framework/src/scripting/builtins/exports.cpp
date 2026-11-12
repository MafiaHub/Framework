/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "exports.h"
#include "../bind/v8_callable.h"
#include "../callback_scope.h"
#include "../engine_helpers.h"
#include "../function_references.h"
#include "../lua/lua_runtime.h"
#include "../resource/resource.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"
#include "../value_transfer.h"

#include <logging/logger.h>

#include <utility>

namespace Framework::Scripting::Builtins {

    struct Exports::Script final {
        static void Register(Bind::Call &call) {
            if (call.ArgCount() < 2) {
                call.ThrowTypeError("Exports.register requires 2 arguments: name, value");
                return;
            }

            if (!call.Arg(0).IsString()) {
                call.ThrowTypeError("Exports.register: name must be a string");
                return;
            }

            ResourceManager *manager = call.GetResourceManager();
            if (!manager) {
                call.ThrowError("Exports.register: resource manager not available");
                return;
            }

            const std::string exportName = call.Arg(0).AsString();
            const bool javaScript        = call.GetLanguage() == Language::JavaScript;

            // Get the current resource context (use stack fallback for async ES modules). A Lua runtime is its
            // resource's own; the manager hands out its resources for reading only, though they are its own to
            // change, as the one it gives a JavaScript call is.
            Resource *currentResource = nullptr;
            if (javaScript) {
                currentResource = manager->GetCurrentResourceWithStackFallback(v8::Isolate::GetCurrent());
            }
            else if (const std::string resourceName = call.ResourceName(); !resourceName.empty()) {
                currentResource = const_cast<Resource *>(std::as_const(*manager).GetResource(resourceName));
            }
            if (!currentResource) {
                call.ThrowError("Exports.register: no resource context");
                return;
            }

            // Check if this export is declared in the manifest
            const bool isInManifest = currentResource->HasExport(exportName);
            if (!isInManifest) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] Registering undeclared export '{}' - add to package.json exports array", currentResource->GetName(), exportName);
            }

            // A name is registered once where Lua has a part in it: by the script that registers it now, or by the
            // one that did. JavaScript replacing what JavaScript registered is what it always did.
            if (isInManifest && (javaScript ? currentResource->GetExportCopy(exportName) != nullptr : currentResource->HasRegisteredExport(exportName))) {
                call.ThrowError("Exports.register: export '" + exportName + "' is already registered");
                return;
            }

            bool registered = false;
            if (javaScript) {
                v8::Isolate *isolate                   = v8::Isolate::GetCurrent();
                const v8::Local<v8::Value> exportValue = Bind::V8ValueOf(call, call.Arg(1));

                // Set the isolate for the resource (needed for storing exports)
                currentResource->SetIsolate(isolate);

                // A function registered directly is reported by this name when it has none of its own.
                if (exportValue->IsFunction()) {
                    FunctionReferences::NameExport(isolate, isolate->GetCurrentContext(), exportValue.As<v8::Function>(), exportName);
                }

                registered = currentResource->RegisterExport(exportName, exportValue);
            }
            else if (isInManifest) {
                // Copied now: every importer is given the value as it was when it was registered.
                auto copied = call.Arg(1).ToTransfer();
                if (!copied) {
                    call.ThrowTypeError("Exports.register: value: " + copied.GetError());
                    return;
                }
                registered = currentResource->RegisterExport(exportName, std::move(copied).GetValue());
            }

            if (!registered) {
                if (!isInManifest) {
                    // RegisterExport failed because it's not in the manifest - this is permissive
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Export '{}' registered (not in manifest)", currentResource->GetName(), exportName);
                    call.Return(true);
                }
                else {
                    // Export is in manifest but RegisterExport still failed - real error (isolate issue)
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("[{}] Failed to register export '{}' - isolate initialization issue", currentResource->GetName(), exportName);
                    call.ThrowError("Exports.register: failed to register export '" + exportName + "' - internal error");
                }
            }
            else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[{}] Export '{}' registered", currentResource->GetName(), exportName);
                call.Return(true);
            }
        }

        static void Get(Bind::Call &call) {
            if (call.ArgCount() < 2) {
                call.ThrowTypeError("Exports.get requires 2 arguments: resourceName, exportName");
                return;
            }

            if (!call.Arg(0).IsString() || !call.Arg(1).IsString()) {
                call.ThrowTypeError("Exports.get: resourceName and exportName must be strings");
                return;
            }

            ResourceManager *manager = call.GetResourceManager();
            if (!manager) {
                call.ThrowError("Exports.get: resource manager not available");
                return;
            }

            const std::string resourceName = call.Arg(0).AsString();
            const std::string exportName   = call.Arg(1).AsString();

            // Get the target resource
            const Resource *resource = manager->GetResource(resourceName);
            if (!resource) {
                call.ThrowError("Exports.get: resource '" + resourceName + "' not found");
                return;
            }

            // Check if the resource is running
            if (!resource->IsRunning()) {
                call.ThrowError("Exports.get: resource '" + resourceName + "' is not running");
                return;
            }

            const std::string notFound = "Exports.get: export '" + exportName + "' not found in resource '" + resourceName + "'";
            const std::string crossing = "Exports.get: export '" + exportName + "' of resource '" + resourceName + "': ";

            // Registered from Lua: a copy, whoever asks.
            if (const std::shared_ptr<const TransferredValue> copy = resource->GetExportCopy(exportName)) {
                auto rebuilt = call.FromTransfer(*copy);
                if (!rebuilt) {
                    call.ThrowError(crossing + rebuilt.GetError());
                    return;
                }
                call.Return(rebuilt.GetValue());
                return;
            }

            if (call.GetLanguage() != Language::JavaScript) {
                if (!resource->HasRegisteredExport(exportName)) {
                    call.ThrowError(notFound);
                    return;
                }
                auto copied = CopyJavaScriptExport(*resource, exportName);
                if (!copied) {
                    call.ThrowError(crossing + copied.GetError());
                    return;
                }
                auto rebuilt = call.FromTransfer(copied.GetValue());
                if (!rebuilt) {
                    call.ThrowError(crossing + rebuilt.GetError());
                    return;
                }
                call.Return(rebuilt.GetValue());
                return;
            }

            v8::Isolate *isolate = v8::Isolate::GetCurrent();

            // Export values are bound to their resource's isolate; nothing registered yet means none.
            v8::Isolate *resourceIsolate = resource->GetIsolate();
            if (resourceIsolate == nullptr) {
                call.ThrowError(notFound);
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
                    call.Return(Bind::V8ValueIn(call, exportValue));
                }
                return;
            }

            // The resource runs in a runtime of its own: the export arrives as a copy, its functions as references.
            FunctionReferences *references = manager->GetFunctionReferences();
            if (references == nullptr) {
                call.ThrowError("Exports.get: resource '" + resourceName + "' is not reachable from here");
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
                call.Return(Bind::V8ValueIn(call, copied));
            }
        }
    };

    const Bind::Namespace &Exports::Definition() {
        static const Bind::Namespace definition = Bind::Namespace("Exports", "Registration and lookup of cross-resource values through the global Exports object.")
                                                      .Function("register", &Script::Register,
                                                          Bind::Doc("Registers a value from the calling resource for use by dependent resources.")
                                                              .Param("name", Bind::TypeOf<std::string>(), "Export name, preferably declared in the current resource manifest.")
                                                              .Param("value", Bind::Type::Custom("unknown", "any"), "JavaScript value or function retained by the current resource.")
                                                              .ReturnType(Bind::TypeOf<bool>())
                                                              .Returns("True when the value was registered."))
                                                      .Function("get", &Script::Get,
                                                          Bind::Doc("Reads one registered export from another running resource; undeclared dependencies produce a warning.")
                                                              .Param("resourceName", Bind::TypeOf<std::string>(), "Name of the running resource that owns the export.")
                                                              .Param("exportName", Bind::TypeOf<std::string>(), "Registered export name.")
                                                              .ReturnType(Bind::Type::Custom("unknown", "any"))
                                                              .Returns("The exported value."));
        return definition;
    }

    void Exports::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, ResourceManager *resourceManager) {
        // The functions carry no manager of their own: a call asks the one of its runtime.
        if (resourceManager != nullptr) {
            Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, target, GetScriptingCatalog(isolate), ScriptingSide(isolate));
    }

    void Exports::RegisterLua(LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

    Utils::Result<TransferredValue, std::string> Exports::CopyJavaScriptExport(const Resource &resource, std::string_view name) {
        using Result = Utils::Result<TransferredValue, std::string>;

        v8::Isolate *isolate = resource.GetIsolate();
        if (isolate == nullptr) {
            return Result::Err("it is not registered");
        }

        // Entered from outside its JavaScript, as EngineScope enters an engine: what reading the value queues there
        // runs as the callback scope closes.
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        const v8::Local<v8::Context> context = resource.GetExportContext();
        if (context.IsEmpty()) {
            return Result::Err("its runtime is not reachable from here");
        }
        v8::Context::Scope contextScope(context);
        const std::unique_ptr<CallbackScope> callbackScope = OpenCallbackScope(isolate);

        const v8::Local<v8::Value> value = resource.GetExportValue(name);
        if (value.IsEmpty()) {
            return Result::Err("it is not registered");
        }
        // A getter of the script may throw while the value is read: the copy says so, and nothing stays pending.
        v8::TryCatch tryCatch(isolate);
        return ValueTransfer::Copy(isolate, context, value, Bind::V8CallableTransfer());
    }

} // namespace Framework::Scripting::Builtins
