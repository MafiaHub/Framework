/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// CRITICAL: Include <cerrno> BEFORE V8 headers on Windows.
// V8 headers include Windows SDK headers that can interfere with
// errno definitions (EINVAL, ERANGE, etc.) needed by <string> and others.
#include <cerrno>

#include <v8.h>

#include "errors.h"

#include <function2/function2.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <utils/lifecycle.h>

namespace Framework::Scripting {

    class ResourceManager; // Forward declaration for resource context tracking
    class FunctionReferences;

    /**
     * Base class for JavaScript engines.
     * Two implementations exist: NodeEngine (server, full Node.js) and
     * V8Engine (client, standalone V8). Shared logic lives here.
     */
    class Engine : public Framework::Lifecycle {
      public:
        using SDKRegisterCallback = fu2::function<void(Engine *) const>;

        /**
         * Initialize the JavaScript engine.
         * @return ScriptingError::SCRIPTING_NONE on success
         */
        [[nodiscard]] virtual ScriptingError Init() = 0;

        /**
         * Shutdown the JavaScript engine.
         */
        void Shutdown() override = 0;

        /**
         * Execute a JavaScript string.
         * @param code JavaScript code to execute
         * @param filename Optional filename for error messages
         * @return true if execution succeeded
         */
        bool Execute(std::string_view code, std::string_view filename = "<eval>");

        /**
         * Execute a JavaScript file.
         * @param filepath Path to the JavaScript file
         * @return true if execution succeeded
         */
        virtual bool ExecuteFile(std::string_view filepath) = 0;

        // Advance microtasks, timers, and host I/O without blocking. Resource lifecycle barriers use
        // this while awaiting async resourceStart/resourceStop handlers.
        virtual void Tick() = 0;

        // Evict cached modules under a resource dir so hot-reload re-reads
        // edited files. No-op without a module cache; call only after stop.
        virtual void EvictModulesUnderPath(const std::string &rootPath) {}

        // Cancel timers the resource created (setTimeout/setInterval) so the
        // shared runtime doesn't keep firing them after stop. Call after stop.
        virtual void ClearResourceTimers(const std::string &resourceName) {}

        // Resource runtimes (docs/scripting_resource_isolation.md). An engine that isolates resources gives each one a
        // runtime of its own; a shared engine runs every resource in its single runtime, and these default to that.

        /**
         * Create the runtime a resource's scripts run in, with the runtime setup callback already run inside it.
         * @return false with the reason in GetLastError().
         */
        virtual bool CreateResourceRuntime(const std::string &resourceName) {
            return true;
        }

        // Destroy it, and with it everything the resource left running there. A runtime that is executing is destroyed
        // at the next tick instead.
        virtual void DestroyResourceRuntime(const std::string &resourceName) {}

        // Run one of a resource's script files in its runtime.
        virtual bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) {
            return ExecuteFile(filepath);
        }

        // The resource an isolate was created for, or empty for a runtime every resource shares.
        virtual std::string GetResourceForIsolate(v8::Isolate *isolate) const {
            return {};
        }

        // Calls and values between runtimes; null for an engine whose resources share one.
        virtual FunctionReferences *GetFunctionReferences() const {
            return nullptr;
        }

        /**
         * Set what installs the bindings in a runtime. Run inside every runtime an isolating engine creates for a
         * resource, with GetIsolate() and GetContext() naming that runtime.
         */
        using RuntimeSetupCallback = fu2::function<void(Engine *) const>;
        void SetRuntimeSetupCallback(RuntimeSetupCallback callback) {
            _runtimeSetupCallback = std::move(callback);
        }

        /**
         * Hear about a resource's runtime just before it is disposed, with its isolate still alive and entered: the
         * place to drop handles a project keeps in it (class caches, pending promises). Listeners live as long as the
         * engine. An engine whose resources share one runtime never calls them.
         */
        using RuntimeDisposingCallback = fu2::function<void(v8::Isolate *) const>;
        void AddRuntimeDisposingListener(RuntimeDisposingCallback callback) {
            _runtimeDisposingListeners.push_back(std::move(callback));
        }

        /**
         * Register framework SDK bindings.
         * Called after Init() to set up Framework.* APIs.
         */
        bool InitFrameworkSDK();

        /**
         * Set callback for registering additional SDK bindings.
         * Used by game projects to add custom APIs.
         */
        void SetSDKRegisterCallback(SDKRegisterCallback callback) {
            _sdkRegisterCallback = std::move(callback);
        }

        /**
         * Get the last error message.
         */
        const std::string &GetLastError() const {
            return _lastError;
        }

        // Escape hatch: raw V8 handles. The caller owns the v8::Locker / Isolate::Scope / HandleScope /
        // Context::Scope setup before touching these — prefer Execute() and the SDK-register callback,
        // which establish the scopes for you. An engine that isolates resources names the runtime the
        // thread is executing in, or its own when it is in none.
        virtual v8::Isolate *GetIsolate() const = 0;
        virtual v8::Local<v8::Context> GetContext() const = 0;

        /**
         * Set the owning ResourceManager for resource context tracking.
         * Called by ResourceManager during construction.
         */
        void SetResourceManager(ResourceManager *mgr) {
            _resourceManager = mgr;
        }

        /**
         * Get the owning ResourceManager.
         */
        ResourceManager *GetResourceManager() const {
            return _resourceManager;
        }

      protected:
        std::string _lastError;
        SDKRegisterCallback _sdkRegisterCallback;
        RuntimeSetupCallback _runtimeSetupCallback;
        std::vector<RuntimeDisposingCallback> _runtimeDisposingListeners;
        ResourceManager *_resourceManager = nullptr;
    };

} // namespace Framework::Scripting
