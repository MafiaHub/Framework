#pragma once

// CRITICAL: Include <cerrno> BEFORE V8 headers on Windows.
// V8 headers include Windows SDK headers that can interfere with
// errno definitions (EINVAL, ERANGE, etc.) needed by <string> and others.
#include <cerrno>

#include <v8.h>

#include <memory>
#include <string>
#include <functional>

#include <utils/lifecycle.h>

namespace Framework::Scripting {

    /**
     * Base class for JavaScript engines.
     * Two implementations exist: NodeEngine (server, full Node.js) and
     * V8Engine (client, standalone V8). Shared logic lives here.
     */
    class Engine : public Framework::Lifecycle {
      public:
        using SDKRegisterCallback = std::function<void(Engine *)>;

        /**
         * Initialize the JavaScript engine.
         * @return true if initialization succeeded
         */
        virtual bool Init() = 0;

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
        bool Execute(const std::string &code, const std::string &filename = "<eval>");

        /**
         * Execute a JavaScript file.
         * @param filepath Path to the JavaScript file
         * @return true if execution succeeded
         */
        virtual bool ExecuteFile(const std::string &filepath) = 0;

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

        /**
         * Get the V8 isolate.
         */
        virtual v8::Isolate *GetIsolate() const = 0;

        /**
         * Get the main context.
         */
        virtual v8::Local<v8::Context> GetContext() const = 0;

        /**
         * Get the Framework global object for binding APIs.
         */
        v8::Local<v8::Object> GetFrameworkObject() const;

        /**
         * Get the Core global object for builtin types and events.
         */
        v8::Local<v8::Object> GetCoreObject() const;

      protected:
        std::string _lastError;
        SDKRegisterCallback _sdkRegisterCallback;
    };

} // namespace Framework::Scripting
