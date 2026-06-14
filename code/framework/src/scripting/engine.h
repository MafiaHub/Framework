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

#include <function2.hpp>

#include <memory>
#include <string>
#include <string_view>

#include <utils/lifecycle.h>

namespace Framework::Scripting {

    class ResourceManager; // Forward declaration for resource context tracking

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
        // which establish the scopes for you.
        virtual v8::Isolate *GetIsolate() const = 0;
        virtual v8::Local<v8::Context> GetContext() const = 0;

        /**
         * Get the Framework global object for binding APIs.
         */
        v8::Local<v8::Object> GetFrameworkObject() const;

        /**
         * Get the Core global object for builtin types and events.
         */
        v8::Local<v8::Object> GetCoreObject() const;

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
        ResourceManager *_resourceManager = nullptr;
    };

} // namespace Framework::Scripting
